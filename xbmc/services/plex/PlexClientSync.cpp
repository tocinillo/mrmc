/*
 *      Copyright (C) 2017 Team MrMC
 *      https://github.com/MrMC
 *      based from EmbyMediaImporter.cpp
 *      Copyright (C) 2016 Team XBMC
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with MrMC; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

#include "PlexClientSync.h"

#include "PlexServices.h"
#include "PlexClient.h"
#include "PlexUtils.h"

#include "GUIUserMessages.h"
#include "filesystem/File.h"
#include "filesystem/CurlFile.h"
#include "filesystem/DirectoryCache.h"
#include "guilib/GUIWindowManager.h"
#include "settings/Settings.h"
#include "utils/log.h"
#include "utils/StringUtils.h"
#include "utils/Stopwatch.h"
#include "utils/JSONVariantParser.h"
#include "video/VideoInfoTag.h"

#include "contrib/easywsclient/easywsclient.hpp"

static const std::string _elementType = "_elementType";
static const std::string _children = "_children";

static const std::string NotificationContainer = "NotificationContainer";
static const std::string TimelineEntry = "TimelineEntry";
static const std::string StatusNotification = "StatusNotification";
static const std::string ActivityNotification = "ActivityNotification";
static const std::string ProgressNotification = "ProgressNotification";
static const std::string TranscodeSession = "TranscodeSession";
static const std::string PlaySessionStateNotification = "PlaySessionStateNotification";
static const std::string BackgroundProcessingQueueEventNotification = "BackgroundProcessingQueueEventNotification";
static const std::string AutoUpdateNotification = "AutoUpdateNotification";
static const std::string AccountUpdateNotification = "AccountUpdateNotification";

typedef enum MediaImportChangesetType
{
  MediaImportChangesetTypeNone = 0,
  MediaImportChangesetTypeAdded,
  MediaImportChangesetTypeChanged,
  MediaImportChangesetTypeRemoved
} MediaImportChangesetType;

CPlexClientSync::CPlexClientSync(const bool owned, const std::string &name, const std::string &address, const std::string &deviceId, const std::string &accessToken)
  : CThread(StringUtils::Format("PlexClientSync[%s]", name.c_str()).c_str())
  , m_stop(true)
  , m_owned(owned)
  , m_name(name)
  , m_address(address)
  , m_deviceId(deviceId)
  , m_accessToken(accessToken)
  , m_websocket(nullptr)
{
}

CPlexClientSync::~CPlexClientSync()
{
  Stop();
}

void CPlexClientSync::Start()
{
  if (!m_stop)
    return;

  m_stop = false;
  CThread::Create();
}

void CPlexClientSync::Stop()
{
  if (m_stop)
    return;

  m_stop = true;
  m_processSleep.Set();
  CThread::StopThread();
}

void CPlexClientSync::ProcessSyncByPolling()
{
  CLog::Log(LOGDEBUG, "CPlexClientSync:ProcessSyncByPolling connected to %s", m_name.c_str());
  CStopWatch checkUpdatesTimer;
  checkUpdatesTimer.StartZero();
  while (!m_stop)
  {
    int m_updateMins = 15;
    if (m_updateMins > 0 && (checkUpdatesTimer.GetElapsedSeconds() > (60 * m_updateMins)))
    {
      CPlexClientPtr client = CPlexServices::GetInstance().FindClient(m_address);
      if (client && client->GetPresence())
      {
        client->ParseSections(PlexSectionParsing::checkSection);
        if (client->NeedUpdate())
        {
          client->ParseSections(PlexSectionParsing::updateSection);
          g_directoryCache.Clear();
          if (CPlexServices::GetInstance().GetPlayState() == MediaServicesPlayerState::stopped)
          {
            CGUIMessage msg(GUI_MSG_NOTIFY_ALL, 0, 0, GUI_MSG_UPDATE);
            g_windowManager.SendThreadMessage(msg);
          }
        }
      }
      checkUpdatesTimer.Reset();
    }

    m_processSleep.WaitMSec(250);
    m_processSleep.Reset();
  }
}

void CPlexClientSync::ProcessSyncByWebSockets()
{
  CURL curl(m_address);
  if (curl.GetProtocol() == "http")
    curl.SetProtocol("ws");
  else if (curl.GetProtocol() == "https")
    curl.SetProtocol("wss");
  curl.SetFileName(":/websockets/notifications?X-Plex-Token=" + m_accessToken);

  static const int WebSocketTimeoutMs = 100;

  static const std::string NotificationMessageType = "MessageType";
  static const std::string NotificationData = "Data";
  static const std::string NotificationMessageTypeUserUpdated = "UserUpdated";
  static const std::string NotificationMessageTypeSessionEnded = "SessionEnded";
  static const std::string NotificationMessageTypeLibraryChanged = "LibraryChanged";
  static const std::string NotificationMessageTypeUserDataChanged = "UserDataChanged";
  static const std::string NotificationMessageTypePlaybackStart = "PlaybackStart";
  static const std::string NotificationMessageTypePlaybackStopped = "PlaybackStopped";
  static const std::string NotificationMessageTypeScheduledTaskEnded = "ScheduledTaskEnded";
  static const std::string NotificationLibraryChangedItemsAdded = "ItemsAdded";
  static const std::string NotificationLibraryChangedItemsUpdated = "ItemsUpdated";
  static const std::string NotificationLibraryChangedItemsRemoved = "ItemsRemoved";
  static const std::string NotificationUserDataChangedUserDataList = "UserDataList";
  static const std::string NotificationUserDataChangedUserDataItemId = "ItemId";

  struct ChangedLibraryItem
  {
    std::string itemId;
    MediaImportChangesetType changesetType;
  };

  m_websocket = easywsclient::WebSocket::from_url(curl.Get() /* TODO: , origin */);
  if (!m_websocket)
  {
    CLog::Log(LOGERROR, "CPlexClientSync:ProcessSyncByWebSockets connection failed from %s", m_name.c_str());
    m_stop = true;
  }
  else
    CLog::Log(LOGDEBUG, "CPlexClientSync:ProcessSyncByWebSockets connected to %s", m_name.c_str());

  while (!m_stop && m_websocket->getReadyState() != easywsclient::WebSocket::CLOSED)
  {
    m_websocket->poll(WebSocketTimeoutMs);
    m_websocket->dispatch(
      [this](const std::string& msg)
      {
        CVariant msgObject;
        if (!CJSONVariantParser::Parse(msg, msgObject))
        {
          CLog::Log(LOGERROR, "CPlexClientSync:ProcessSyncByWebSockets invalid msg %s from %s",
            msg.c_str(), m_name.c_str());
          return;
        }

        if (msgObject.isMember(_elementType))
        {
          // old style messages
          CLog::Log(LOGDEBUG, "CPlexClientSync:ProcessSyncByWebSockets ignoring old style msg %s", msg.c_str());
          return;
        }
        else if (msgObject.isMember(NotificationContainer))
        {
          // new style messages
          CVariant variant = msgObject[NotificationContainer];
          if (variant.isMember(TimelineEntry))
          {
            // "identifier" contains tv.plex, ignore them.

            // "type":"1" -> movie
            // "type":"2" -> series
            // "type":"3" -> season ?
            // "type":"4" -> episode
            // "type":"8" -> artist (band)
            // "type":"9" -> album
            // "type":"10" -> track (song)
            // "type":"12" -> trailer, extras?
            // "type":"14" -> photo

            // 'itemID'
            // some state get reported before they actually happen (wtf?)
            // "state":"0" -> created
            // "state":"1" -> ???
            // "state":"2" -> matching
            // "state":"3" -> downloading
            // "state":"4" -> loading
            // "state":"5" -> finished
            // "state":"6" -> analyzing
            // "state":"9" -> deleted
            CVariant timelineEntry = variant[TimelineEntry];
            if (!timelineEntry.isNull())
            {
              for (auto variantIt = timelineEntry.begin_array(); variantIt != timelineEntry.end_array(); ++variantIt)
              {
                if (*variantIt != CVariant::VariantTypeNull)
                {
                  const CVariant timeline = *variantIt;
                  std::string itemID = timeline["itemID"].asString();
                  std::string sectionID = timeline["sectionID"].asString();
                  std::string type = timeline["type"].asString();
                  std::string state = timeline["state"].asString();
                  std::string title = timeline["title"].asString();
                  std::string identifier = timeline["identifier"].asString();
                  std::string updatedAt = timeline["updatedAt"].asString();
                  CLog::Log(LOGDEBUG, "CPlexClientSync:ProcessSyncByWebSockets TimelineEntry: "
                    "sectionID(%s), itemID(%s), type(%s), state(%s), title(%s), identifier(%s)",
                    sectionID.c_str(), itemID.c_str(), type.c_str(), state.c_str(), title.c_str(), identifier.c_str());
                }
              }
            }
          }
          else if (variant.isMember(ActivityNotification))
          {
            // even more messages we do not care about
            CVariant progress = variant[ActivityNotification];
            CLog::Log(LOGDEBUG, "CPlexClientSync:ProcessSyncByWebSockets ActivityNotification: msg %s", msg.c_str());
          }
          else if (variant.isMember(PlaySessionStateNotification))
          {
            CVariant playSessionState = variant[PlaySessionStateNotification]; // is array
            if (playSessionState.isArray())
            {
              for (auto item = playSessionState.begin_array(); item != playSessionState.end_array(); ++item)
              {
                const std::string key = (*item)["key"].asString();
                const std::string state = (*item)["state"].asString();
                const std::string sessionKey = (*item)["sessionKey"].asString();
                CLog::Log(LOGDEBUG, "CPlexClientSync:ProcessSyncByWebSockets PlaySessionStateNotification:sessionKey=%s, state=%s",
                  sessionKey.c_str(), state.c_str());
              }
            }
          }
          else if (variant.isMember(StatusNotification))
          {
            // messages we do not care about
            CVariant status = variant[StatusNotification];
          }
          else if (variant.isMember(StatusNotification))
          {
            // more messages we do not care about
            CVariant progress = variant[StatusNotification];
          }
          else if (variant.isMember(ProgressNotification))
          {
            // even more messages we do not care about
            CVariant progress = variant[ProgressNotification];
          }
          else if (variant.isMember(TranscodeSession))
          {
            // even more messages we do not care about
            CVariant progress = variant[TranscodeSession];
            //CLog::Log(LOGDEBUG, "CPlexClientSync:ProcessSyncByWebSockets TranscodeSession: msg %s", msg.c_str());
          }
          else if (variant.isMember(BackgroundProcessingQueueEventNotification))
          {
            // even more messages we do not care about
            CVariant progress = variant[BackgroundProcessingQueueEventNotification];
          }
          else if (variant.isMember(AutoUpdateNotification))
          {
            // even more messages we do not care about
            CVariant progress = variant[AutoUpdateNotification];
          }
          else if (variant.isMember(AccountUpdateNotification))
          {
            // even more messages we do not care about
            CVariant progress = variant[AccountUpdateNotification];
          }
          else
          {
            CLog::Log(LOGDEBUG, "CPlexClientSync:ProcessSyncByWebSockets unknown %s", msg.c_str());
          }
        }
      });
  }

  if (m_websocket)
  {
    m_websocket->close();
    SAFE_DELETE(m_websocket);
  }
  m_stop = true;
}

void CPlexClientSync::Process()
{
  if (m_owned)
    ProcessSyncByWebSockets();
  else
    ProcessSyncByPolling();

  m_stop = true;
}
