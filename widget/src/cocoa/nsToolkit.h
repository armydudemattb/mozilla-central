/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is mozilla.org code.
 *
 * The Initial Developer of the Original Code is
 * Netscape Communications Corporation.
 * Portions created by the Initial Developer are Copyright (C) 1998
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Josh Aas <josh@mozilla.com>
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPL"), or
 * the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */

#include "nsIToolkit.h"
#include "nsIObserver.h"

#include <IOKit/IOKitLib.h>

/**
 * The toolkit abstraction is necessary because the message pump must
 * execute within the same thread that created the widget under Win32.
 * We don't care about that on Mac: we have only one thread for the UI
 * and maybe even for the whole application.
 * 
 * So on the Mac, the nsToolkit used to be a unique object, created once
 * at startup along with nsAppShell and passed to all the top-level
 * windows and it became a convenient place to throw in everything we
 * didn't know where else to put, like the PLEvent queue and
 * the handling of global pointers on some special widgets (focused
 * widget, widget hit, widget pointed).
 *
 * All this has changed: the application now usually creates one copy of
 * the nsToolkit per window and the special widgets had to be moved
 * to the nsMacEventHandler. Also, to avoid creating several repeaters,
 * the PLEvent queue has been moved to a global object of its own.
 *
 * If by any chance we support one day several threads for the UI
 * on the Mac, will have to create one instance of the PLEvent queue
 * per nsToolkit.
 */

#define MAC_OS_X_VERSION_10_2_HEX 0x00001020
#define MAC_OS_X_VERSION_10_3_HEX 0x00001030
#define MAC_OS_X_VERSION_10_4_HEX 0x00001040

class nsToolkit : public nsIToolkit, public nsIObserver
{
public:
                     nsToolkit();
  virtual            ~nsToolkit();

  NS_DECL_ISUPPORTS
  NS_DECL_NSITOOLKIT
  NS_DECL_NSIOBSERVER

  // Returns the OS X version as returned from Gestalt(gestaltSystemVersion, ...)
  static long        OSXVersion();
  
  static void        PostSleepWakeNotification(const char* aNotification);

protected:

  nsresult           RegisterForSleepWakeNotifcations();
  void               RemoveSleepWakeNotifcations();

protected:

  static void        SetupQuartzRendering();

protected:

  bool               mInited;

  CFRunLoopSourceRef mSleepWakeNotificationRLS;
  io_object_t        mPowerNotifier;
};

extern nsToolkit* NS_CreateToolkitInstance();
