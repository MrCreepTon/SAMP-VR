#include "sa_main.h"
#include <math.h>
#include <windows.h>
#include <stdio.h>
#include <MinHook.h>
#include <thread>
#include <mutex>

# define M_PI           3.14159265358979323846

namespace sa {

	sActions vrActions;

	bool init = false;

	vr::TrackedDevicePose_t tracked_device_pose[vr::k_unMaxTrackedDeviceCount];

	vr::HmdQuaternion_t GetRotation(const vr::HmdMatrix34_t& matrix) {
		vr::HmdQuaternion_t q{};

		q.w = sqrt(fmax(0, 1 + matrix.m[0][0] + matrix.m[1][1] + matrix.m[2][2])) / 2;
		q.x = sqrt(fmax(0, 1 + matrix.m[0][0] - matrix.m[1][1] - matrix.m[2][2])) / 2;
		q.y = sqrt(fmax(0, 1 - matrix.m[0][0] + matrix.m[1][1] - matrix.m[2][2])) / 2;
		q.z = sqrt(fmax(0, 1 - matrix.m[0][0] - matrix.m[1][1] + matrix.m[2][2])) / 2;

		q.x = copysign(q.x, matrix.m[2][1] - matrix.m[1][2]);
		q.y = copysign(q.y, matrix.m[0][2] - matrix.m[2][0]);
		q.z = copysign(q.z, matrix.m[1][0] - matrix.m[0][1]);

		return q;
	}

	void GetRotationInEuler(const vr::HmdMatrix34_t& matrix, double& yaw, double& pitch, double& roll) {
		vr::HmdQuaternion_t q = GetRotation(matrix);

		q.x = int(q.x * 100 + 0.5) / 100.0;
		q.y = int(q.y * 100 + 0.5) / 100.0;
		q.z = int(q.z * 100 + 0.5) / 100.0;
		q.w = int(q.w * 100 + 0.5) / 100.0;

		const auto x = q.x;
		const auto y = q.y;
		const auto z = q.z;
		const auto w = q.w;

		// roll (x-axis rotation)
		double sinr_cosp = 2 * (w * x + y * z);
		double cosr_cosp = 1 - 2 * (x * x + y * y);
		roll = atan2f(sinr_cosp, cosr_cosp);

		// pitch (y-axis rotation)
		double sinp = 2 * (w * y - z * x);
		if (fabs(sinp) >= 1)
			pitch = copysignf(M_PI / 2, sinp); // use 90 degrees if out of range
		else
			pitch = asinf(sinp);

		// yaw (z-axis rotation)
		double siny_cosp = 2 * (w * z + x * y);
		double cosy_cosp = 1 - 2 * (y * y + z * z);
		yaw = atan2f(siny_cosp, cosy_cosp);
	}

	sampapi::CVector rotation(0, 0, 0);

	void SetEntityAlpha(void* pEntity, int alpha) {
		((void(__cdecl*)(void*, int))0x5332C0)(pEntity, alpha);
	}

	struct sKeyValue {
		byte value;
		DWORD timeSinceLastUpdate = GetTickCount();
	};

	struct sKeyValues {
		sKeyValue forward = { 0 };
		sKeyValue right = { 0 };
		sKeyValue sprint = { 0 };
		sKeyValue jump = { 0 };
		sKeyValue crouch = { 0 };
		sKeyValue enterexit = { 0 };
		sKeyValue hit = { 0 };
	} keyValues;

	struct sRotationInput {
		float rotationOffset = 1;
		float rotationOffsetRadians = 0;
		bool isRotating = false;
		float valueToAdd = 0;
	} rotationInput;

	void SetGameKeyState(BYTE key, BYTE state)
	{
		*(uint8_t*)(0xB73458 + key) = state;
	}

	void SetFov(float fov) {
		*(float*)0x0858CE0 = fov;
		*(float*)0x0B6F250 = fov;
	}

	// Code below is from hell. GameKey wont work for unknown reason (maybe needed special values for some keys)
	void EmulateLeftMouse(int mouseEvent) {
		INPUT Input = { INPUT_MOUSE, 0, 0, 0, mouseEvent, 0, 0 };
		SendInput(1, &Input, sizeof(INPUT));
	}

	void EmulateKeyboard(char key, int mode) {
		keybd_event(key, 0, mode, 0);
	}

	typedef char(__fastcall* tCPadUpdate)(void* _this, int a2);
	tCPadUpdate origCPadUpdate = nullptr;
	char __fastcall Hooked_CPadUpdate(void* _this, int a2) {

		SetGameKeyState(0x1, keyValues.right.value);
		SetGameKeyState(0x3, keyValues.forward.value);
		SetGameKeyState(0x20, keyValues.sprint.value);
		
		if (keyValues.enterexit.value != 0)
			EmulateKeyboard('F', 0);
		if (keyValues.hit.value != 0)
			EmulateLeftMouse(MOUSEEVENTF_LEFTDOWN);
		if (keyValues.crouch.value != 0)
			EmulateKeyboard('C', 0);
		if (keyValues.jump.value != 0)
			EmulateKeyboard(0xA0, 0);

		DWORD currentTick = GetTickCount();
		DWORD resetDelay = 100;
		if (keyValues.crouch.value != 0 && currentTick - keyValues.crouch.timeSinceLastUpdate > resetDelay) {
			keyValues.crouch.value = 0;
			EmulateKeyboard('C', KEYEVENTF_KEYUP);
		}

		if (keyValues.jump.value != 0 && currentTick - keyValues.jump.timeSinceLastUpdate > resetDelay) {
			keyValues.jump.value = 0;
			EmulateKeyboard(0xA0, KEYEVENTF_KEYUP);
		}

		if (keyValues.hit.value != 0 && currentTick - keyValues.hit.timeSinceLastUpdate > resetDelay) {
			keyValues.hit.value = 0;
			EmulateLeftMouse(MOUSEEVENTF_LEFTUP);
		}

		if (keyValues.enterexit.value != 0 && currentTick - keyValues.enterexit.timeSinceLastUpdate > resetDelay) {
			keyValues.enterexit.value = 0;
			EmulateKeyboard('F', KEYEVENTF_KEYUP);
		}

		return origCPadUpdate(_this, a2);
	}

	typedef void(__thiscall* tCEndityRender)(void* entity);
	tCEndityRender origCEntityRender = nullptr;
	void __fastcall HookedCEntityRender(void* entity) {
		if (entity != sampapi::v037r1::RefGame()->GetPlayerPed()->m_pGameEntity)
			return origCEntityRender(entity);
	}

	typedef void(__thiscall* tDrawHud)();
	tDrawHud origDrawHud = nullptr;
	void __fastcall Hooked_DrawHud() {
		return;
	}

	typedef void(__thiscall* tDrawRadar)();
	tDrawRadar origDrawRadar = nullptr;
	void __fastcall Hooked_DrawRadar() {
		return;
	}

	float timeSinceLastUpdate = 0;
	bool reset = false;

	bool GetAnalogActionData(vr::VRActionHandle_t& actionHandle, vr::InputAnalogActionData_t& analogDataOut)
	{
		vr::EVRInputError result = vr::VRInput()->GetAnalogActionData(actionHandle, &analogDataOut, sizeof(analogDataOut), vr::k_ulInvalidInputValueHandle);

		if (result == vr::VRInputError_None)
			return true;

		return false;
	}

	bool PressedDigitalAction(vr::VRActionHandle_t& actionHandle, bool checkIfActionChanged)
	{
		vr::InputDigitalActionData_t digitalActionData;
		vr::EVRInputError result = vr::VRInput()->GetDigitalActionData(actionHandle, &digitalActionData, sizeof(digitalActionData), vr::k_ulInvalidInputValueHandle);

		if (result == vr::VRInputError_None)
		{
			if (checkIfActionChanged)
				return digitalActionData.bState && digitalActionData.bChanged;
			else
				return digitalActionData.bState;
		}

		return false;
	}

	void EmulateVirtualKey(BYTE key, bool isPressed) {
		keybd_event(
			key,
			0,
			isPressed ? 0 : KEYEVENTF_KEYUP,
			0
		);
	}

	bool jumping = false;

	// todo: make it to make hud fully visible in VR
	void MoveHud(int offsetX, int offsetY) {
		//float value = *(float*)(0x58EE87);
		//*(float*)(0x86535C) = *(float*)(0x58EE87) + 0.1f;
	}

	void OnVR(vr::IVRSystem* vr_context, float renderWidth, float renderHeight) {
		namespace samp = sampapi::v037r1;
		
		if (!vr_context)
			return;

		vr::VRCompositor()->PostPresentHandoff();
		vr::VRCompositor()->WaitGetPoses(tracked_device_pose, vr::k_unMaxTrackedDeviceCount, NULL, 0);

		
		if (init) {

			auto GAME = samp::RefGame();
			auto pCamera = GAME->GetCamera();
			auto pPlayerPed = GAME->GetPlayerPed();

			for (int nDevice = 0; nDevice < vr::k_unMaxTrackedDeviceCount; nDevice++) {
				if ((tracked_device_pose[nDevice].bDeviceIsConnected) && (tracked_device_pose[nDevice].bPoseIsValid))
				{
					if (vr_context->GetTrackedDeviceClass(nDevice) == vr::TrackedDeviceClass_HMD) {

						double sy = sqrt(pow(tracked_device_pose[nDevice].mDeviceToAbsoluteTracking.m[0][0], 2) + pow(tracked_device_pose[nDevice].mDeviceToAbsoluteTracking.m[0][1], 2));
						double roll = atan2(tracked_device_pose[nDevice].mDeviceToAbsoluteTracking.m[1][2], tracked_device_pose[nDevice].mDeviceToAbsoluteTracking.m[2][2]);
						double pitch = atan2(tracked_device_pose[nDevice].mDeviceToAbsoluteTracking.m[0][2], sy);
						double yaw = atan2(tracked_device_pose[nDevice].mDeviceToAbsoluteTracking.m[0][1], tracked_device_pose[nDevice].mDeviceToAbsoluteTracking.m[0][0]);

						sampapi::CVector position;
						pPlayerPed->GetBonePosition(6, &position);

						sampapi::CVector lookAt(position.x, position.y, position.z);
						float multiplier = 3.0f;

						float xAmplifier = -sinf(pitch);
						float yAmplifier = cosf(yaw) * cosf(pitch);
						float zAmplifier = -1 * cosf(pitch) * sinf(roll);
				
						lookAt.x += xAmplifier * multiplier;
						lookAt.y += yAmplifier * multiplier;
						lookAt.z += zAmplifier * multiplier;

						float additionalAngle = atan2f(lookAt.x - position.x, lookAt.y - position.y) + rotationInput.rotationOffsetRadians;
						lookAt.x = position.x + sinf(additionalAngle) * multiplier;
						lookAt.y = position.y + cosf(additionalAngle) * multiplier;

						position.x += sinf(additionalAngle) / 5;
						position.y += cosf(additionalAngle) / 5;

						GAME->GetCamera()->Set(position, rotation);
						GAME->GetCamera()->PointAt(lookAt, 2);
					}

					if (vr_context->GetTrackedDeviceClass(nDevice) == vr::TrackedDeviceClass_Controller) {
						if (GAME->IsMenuVisible()) {
							float posX = tracked_device_pose->mDeviceToAbsoluteTracking.m[0][0], posY = tracked_device_pose->mDeviceToAbsoluteTracking.m[0][1], posZ = tracked_device_pose->mDeviceToAbsoluteTracking.m[0][2];
							float screenPosX = renderWidth * posX / posZ;
							float screenPosY = renderHeight * posY / posZ;
							SetCursorPos((int)screenPosX, (int)screenPosY);
						}
					}
				}

			}

			vr::InputAnalogActionData_t analogActionData;

			if (PressedDigitalAction(vrActions.actionCrouch, true)) {
				keyValues.crouch.value = 255;
				keyValues.crouch.timeSinceLastUpdate = GetTickCount();
			}

			if (PressedDigitalAction(vrActions.actionEnterExit, true)) {
				keyValues.enterexit.value = 255;
				keyValues.enterexit.timeSinceLastUpdate = GetTickCount();
			}

			if (PressedDigitalAction(vrActions.actionHit, true)) {
				keyValues.hit.value = 255;
				keyValues.hit.timeSinceLastUpdate = GetTickCount();
			}

			if (pPlayerPed != nullptr && pPlayerPed->DoesExist()) {
				if (GetAnalogActionData(vrActions.actionWalk, analogActionData)) {

					if (pPlayerPed != nullptr && pPlayerPed->DoesExist()) {
						sampapi::CMatrix matrix;
						pPlayerPed->GetMatrix(&matrix);

						if (analogActionData.bActive) {
							float deadZone = 0.2f;
							float sprintZone = 0.8f;
							int right = 0, forward = 0, sprint = 0;
							if (fabs(analogActionData.x) > deadZone)
								right = -255 * (analogActionData.x < 0 ? -1 : 1);
							if (fabs(analogActionData.y) > deadZone)
								forward = 255 * (analogActionData.y < 0 ? -1 : 1);
							if (fabs(analogActionData.x) > sprintZone || fabs(analogActionData.y) > sprintZone)
								sprint = 255;

							keyValues.right.value = right;
							keyValues.forward.value = forward;
							keyValues.sprint.value = sprint;

						}
					}
				}

				if (GetAnalogActionData(vrActions.actionRotate, analogActionData)) {
					if (analogActionData.bActive) {
						float deadZone = 0.2f;
						if (jumping) {
							if (fabs(analogActionData.y) < deadZone)
								jumping = false;
						}
						else {
							if (fabs(analogActionData.y) > deadZone) {
								jumping = true;
								keyValues.jump.value = 255;
								keyValues.jump.timeSinceLastUpdate = GetTickCount();
							}
						}
						if (rotationInput.isRotating) {
							if (fabs(analogActionData.x) < deadZone) {
								rotationInput.isRotating = false;
								rotationInput.rotationOffset += rotationInput.valueToAdd;
								rotationInput.valueToAdd = 0;
								while (fabs(rotationInput.rotationOffset) > 360) {
									rotationInput.rotationOffset = fabs(rotationInput.rotationOffset - 360) * (rotationInput.rotationOffset < 0 ? -1 : 1);
								}
								rotationInput.rotationOffsetRadians = rotationInput.rotationOffset * (M_PI / 180);
							}
						}
						else {
							if (fabs(analogActionData.x) > deadZone) {
								rotationInput.isRotating = true;
								rotationInput.valueToAdd = 45 * (analogActionData.x < 0 ? -1 : 1);
							}
						}

					}
				}
			}
		}
		else {
			if (*reinterpret_cast<unsigned char*>(0xC8D4C0) != 9)
				return;

			MH_Initialize();
			MH_CreateHook((void*)0x0058EAF0, &Hooked_DrawHud, (void**)&origDrawHud);
			MH_EnableHook((void*)0x0058EAF0);
			MoveHud(-100, 0);

			MH_CreateHook((void*)0x0058A330, &Hooked_DrawRadar, (void**)&origDrawRadar);
			MH_EnableHook((void*)0x0058A330);
			
			// With this we can't see hands :c
			//MH_CreateHook((void*)0x534310, &HookedCEntityRender, (void**)&origCEntityRender);
			//MH_EnableHook((void*)0x534310);

			unsigned long dwProtect[2];
			MH_CreateHook((void*)0x541C40, &Hooked_CPadUpdate, (void**)&origCPadUpdate);
			MH_EnableHook((void*)0x541C40);

			void* pAddr = reinterpret_cast<void*>(0x6FF452);
			VirtualProtect(pAddr, 6, PAGE_EXECUTE_READWRITE, &dwProtect[0]);
			memset(pAddr, 0x90, 6);
			VirtualProtect(pAddr, 6, dwProtect[0], &dwProtect[1]);
			float* pAddr2 = reinterpret_cast<float*>(0xC3EFA4);
			VirtualProtect(pAddr2, 6, PAGE_EXECUTE_READWRITE, &dwProtect[0]);
			*pAddr2 = 16.0f / 8.0f;
			VirtualProtect(pAddr2, 6, dwProtect[0], &dwProtect[1]);

			pAddr = reinterpret_cast<void*>(sampapi::GetBase() + 0x9D9D0);
			VirtualProtect(pAddr, 4, PAGE_EXECUTE_READWRITE, &dwProtect[0]);
			memset(pAddr, 0x5051FF15, 4);
			VirtualProtect(pAddr, 4, dwProtect[0], &dwProtect[1]);

			init = true;
		}
	}

}