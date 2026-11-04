// Existing terminal palette shared without importing the dashboard, telemetry, files or platform APIs.
#pragma once
#include <cstdio>
#include <string>
namespace om {
enum class ColorLevel { None, Basic16, Palette256, TrueColor };
/** What a piece of text means; the terminal decides how it looks. */
enum class Tone { Plain, Dark, Rule, Canary, Donation, Amber, Red, Green };
namespace dash_detail {
struct ToneCodes {int r,g,b,c256,c16;};
inline ToneCodes toneCodes(Tone t){
    switch(t){
        case Tone::Dark: return {0x6e,0x6e,0x6e,243,90};
        case Tone::Rule: return {0x6e,0x6e,0x6e,243,90};
        case Tone::Canary: return {0xff,0xd2,0x1f,220,93};
        case Tone::Donation: return {0xff,0xf3,0xb0,230,97};
        case Tone::Amber: return {0xb8,0x86,0x0b,136,33};
        case Tone::Red: return {0xd9,0x53,0x4f,167,91};
        case Tone::Green: return {0x5f,0xff,0x5f,83,92};
        default: return {0,0,0,0,0};
    }
}
/** Legacy rules only; typed layout never substitutes glyphs after measuring their cells. */
inline bool g_dashBoxRules=true;
inline std::string paint(const std::string &text,Tone tone,ColorLevel level){
    if(level==ColorLevel::None||tone==Tone::Plain||text.empty())return text;
    const ToneCodes c=toneCodes(tone);char open[48];
    const bool viewerPalette=tone!=Tone::Canary&&tone!=Tone::Donation;
    switch(viewerPalette?ColorLevel::Basic16:level){
        case ColorLevel::TrueColor: snprintf(open,sizeof(open),"\x1b[38;2;%d;%d;%dm",c.r,c.g,c.b);break;
        case ColorLevel::Palette256: snprintf(open,sizeof(open),"\x1b[38;5;%dm",c.c256);break;
        default: snprintf(open,sizeof(open),"\x1b[%dm",c.c16);break;
    }
    if(tone==Tone::Rule&&g_dashBoxRules){std::string line;for(char ch:text)line+=ch=='-'?std::string("\xe2\x94\x80"):std::string(1,ch);return std::string(open)+line+"\x1b[0m";}
    return std::string(open)+text+"\x1b[0m";
}
inline std::string paintLiteral(const std::string &text,Tone tone,ColorLevel level){return paint(text,tone==Tone::Rule?Tone::Dark:tone,level);}
} // namespace dash_detail
} // namespace om
