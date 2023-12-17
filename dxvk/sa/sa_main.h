#ifndef SA_MAIN_H
#define SA_MAIN_H

#pragma comment (lib, "sampapi.lib")

#include "sampapi/CGame.h"
#include <openvr/openvr.h>
#include <corecrt_math.h>

namespace sa {

	struct sActions {
		vr::VRActionHandle_t actionWalk;
		vr::VRActionHandle_t actionRotate;
		vr::VRActionHandle_t actionCrouch;
		vr::VRActionHandle_t actionEnterExit;
		vr::VRActionHandle_t actionHit;
	};

	void OnVR(vr::IVRSystem* vr_context, float renderWidth, float renderHeight);

	extern sActions vrActions;
}

#endif