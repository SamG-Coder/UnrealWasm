#pragma once
#include "GenericPlatform/GenericPlatformInput.h"

struct FWebCudaPlatformInput : FGenericPlatformInput
{
    static uint32 GetKeyMap(uint32* Codes,FString* Names,uint32 Max)
    {
        uint32 Count=0;
        auto Add=[&](uint32 Code,const TCHAR* Name){if(Count<Max){Codes[Count]=Code;Names[Count++]=Name;}};
        const TCHAR* Digits[]={TEXT("Zero"),TEXT("One"),TEXT("Two"),TEXT("Three"),TEXT("Four"),TEXT("Five"),TEXT("Six"),TEXT("Seven"),TEXT("Eight"),TEXT("Nine")};
        for(uint32 I=0;I<10;++I)Add(48+I,Digits[I]);
        for(uint32 I=0;I<26;++I){TCHAR Letter[]={TCHAR('A'+I),0};Add(65+I,Letter);}
        for(uint32 I=0;I<10;++I)Add(96+I,*FString::Printf(TEXT("NumPad%u"),I));
        for(uint32 I=0;I<12;++I)Add(112+I,*FString::Printf(TEXT("F%u"),I+1));
        Add(8,TEXT("BackSpace"));Add(9,TEXT("Tab"));Add(13,TEXT("Enter"));Add(19,TEXT("Pause"));
        Add(20,TEXT("CapsLock"));Add(27,TEXT("Escape"));Add(32,TEXT("SpaceBar"));
        Add(33,TEXT("PageUp"));Add(34,TEXT("PageDown"));Add(35,TEXT("End"));Add(36,TEXT("Home"));
        Add(37,TEXT("Left"));Add(38,TEXT("Up"));Add(39,TEXT("Right"));Add(40,TEXT("Down"));
        Add(45,TEXT("Insert"));Add(46,TEXT("Delete"));Add(91,TEXT("LeftCommand"));Add(92,TEXT("RightCommand"));
        Add(106,TEXT("Multiply"));Add(107,TEXT("Add"));Add(109,TEXT("Subtract"));Add(110,TEXT("Decimal"));Add(111,TEXT("Divide"));
        Add(144,TEXT("NumLock"));Add(145,TEXT("ScrollLock"));
        Add(160,TEXT("LeftShift"));Add(161,TEXT("RightShift"));Add(162,TEXT("LeftControl"));Add(163,TEXT("RightControl"));
        Add(164,TEXT("LeftAlt"));Add(165,TEXT("RightAlt"));
        Add(186,TEXT("Semicolon"));Add(187,TEXT("Equals"));Add(188,TEXT("Comma"));Add(189,TEXT("Hyphen"));
        Add(190,TEXT("Period"));Add(191,TEXT("Slash"));Add(192,TEXT("Tilde"));Add(219,TEXT("LeftBracket"));
        Add(220,TEXT("Backslash"));Add(221,TEXT("RightBracket"));Add(222,TEXT("Apostrophe"));
        return Count;
    }
    static uint32 GetCharKeyMap(uint32* Codes,FString* Names,uint32 Max)
    {
        return GetStandardPrintableKeyMap(Codes,Names,Max,true,true);
    }
};
using FPlatformInput=FWebCudaPlatformInput;
