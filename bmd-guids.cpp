// Defines the BMD COM GUIDs that the ATEM SDK declares but never provides
// compiled definitions for (no _i.c file ships with the SDK).
#define INITGUID
#include <guiddef.h>

// IIDs extracted from BMDSwitcherAPI.h MIDL_INTERFACE() attributes

// IBMDSwitcherDiscovery  "00EED297-E1B5-407A-AE96-6CB93D33B7F2"
DEFINE_GUID(IID_IBMDSwitcherDiscovery,
    0x00EED297, 0xE1B5, 0x407A, 0xAE, 0x96, 0x6C, 0xB9, 0x3D, 0x33, 0xB7, 0xF2);

// IBMDSwitcherMacroPoolCallback  "E29294A0-FB4C-418D-9AE1-C6CBA288104F"
DEFINE_GUID(IID_IBMDSwitcherMacroPoolCallback,
    0xE29294A0, 0xFB4C, 0x418D, 0x9A, 0xE1, 0xC6, 0xCB, 0xA2, 0x88, 0x10, 0x4F);

// IBMDSwitcherMacroPool  "5FA28BFC-7934-42F4-BED8-8744D62D210F"
DEFINE_GUID(IID_IBMDSwitcherMacroPool,
    0x5FA28BFC, 0x7934, 0x42F4, 0xBE, 0xD8, 0x87, 0x44, 0xD6, 0x2D, 0x21, 0x0F);

// IBMDSwitcherMacroControl  "2E23E657-A7F0-4C4A-BCBE-4B8D3DD061AC"
DEFINE_GUID(IID_IBMDSwitcherMacroControl,
    0x2E23E657, 0xA7F0, 0x4C4A, 0xBC, 0xBE, 0x4B, 0x8D, 0x3D, 0xD0, 0x61, 0xAC);

// CLSID_CBMDSwitcherDiscovery  "3EFEA8DB-282F-4C23-B218-FC8A2FF0861E"
DEFINE_GUID(CLSID_CBMDSwitcherDiscovery,
    0x3EFEA8DB, 0x282F, 0x4C23, 0xB2, 0x18, 0xFC, 0x8A, 0x2F, 0xF0, 0x86, 0x1E);
