/* Shared host/guest menu contract: PS4 system parameter 1000, 1 = Cross. */
#pragma once
#define BBPORT_ENTER_BUTTON_ASSIGN 1
#define BBPORT_CONFIRM_PAD_BUTTON (BBPORT_ENTER_BUTTON_ASSIGN ? 0x4000u : 0x2000u)
