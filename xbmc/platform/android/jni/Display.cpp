/*
 *      Copyright (C) 2014 Team Kodi
 *      http://xbmc.org
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
 *  along with Kodi; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

#include "Display.h"

#include "jutils/jutils-details.hpp"

using namespace jni;

int CJNIDisplayMode::getModeId()
{
  jmethodID mid = get_method_id(m_object, "getModeId", "()I");
  if (mid != NULL)
    return call_method<jint>(m_object, mid);
  else
  {
    xbmc_jnienv()->ExceptionClear();
    return 1080;
  }
}

int CJNIDisplayMode::getPhysicalHeight()
{
  jmethodID mid = get_method_id(m_object, "getPhysicalHeight", "()I");
  if (mid != NULL)
    return call_method<jint>(m_object, mid);
  else
  {
    xbmc_jnienv()->ExceptionClear();
    return 1080;
  }
}

int CJNIDisplayMode::getPhysicalWidth()
{
  jmethodID mid = get_method_id(m_object, "getPhysicalWidth", "()I");
  if (mid != NULL)
    return call_method<jint>(m_object, mid);
  else
  {
    xbmc_jnienv()->ExceptionClear();
    return 1920;
  }
}

float CJNIDisplayMode::getRefreshRate()
{
  jmethodID mid = get_method_id(m_object, "getRefreshRate", "()F");
  if (mid != NULL)
    return call_method<jfloat>(m_object, mid);
  else
  {
    xbmc_jnienv()->ExceptionClear();
    return 60.0f;
  }
}

/*************/

CJNIDisplay::CJNIDisplay()
 : CJNIBase("android.view.Display")
{
}

float CJNIDisplay::getRefreshRate()
{
  return call_method<jfloat>(m_object,
    "getRefreshRate", "()F");
}

std::vector<float> CJNIDisplay::getSupportedRefreshRates()
{
  if (GetSDKVersion() >= 21)
    return jcast<std::vector<float>>(
      call_method<jhfloatArray>(m_object, "getSupportedRefreshRates", "()[F"));
  else
    return std::vector<float>();
}

CJNIDisplayMode CJNIDisplay::getMode()
{
  jhclass clazz = get_class(m_object);
  jmethodID id = get_method_id(clazz, "getMode", "()Landroid/view/Display$Mode;");
  if (id != NULL)
    return call_method<jhobject>(m_object, id);
  else
  {
    xbmc_jnienv()->ExceptionClear();
    return jhobject();
  }
}

int CJNIDisplay::getWidth()
{
  return call_method<jint>(m_object,
    "getWidth", "()I");
}

int CJNIDisplay::getHeight()
{
  return call_method<jint>(m_object,
    "getHeight", "()I");
}

std::vector<CJNIDisplayMode> CJNIDisplay::getSupportedModes()
{
  jhclass clazz = get_class(m_object);
  jmethodID id = get_method_id(clazz, "getSupportedModes", "()[Landroid/view/Display$Mode;");
  if (id != NULL)
    return jcast<CJNIDisplayModes>(call_method<jhobjectArray>(m_object, id));
  else
  {
    xbmc_jnienv()->ExceptionClear();
    return CJNIDisplayModes();
  }
}

