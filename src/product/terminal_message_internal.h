// Internal checked catalog bridge shared by fixed domains. Not an external/runtime-template or review-authority API.
#pragma once
#include "terminal_message.h"
namespace om { namespace terminal_message_detail {
struct Templates {
    i18n::LocalizedMessage requested,english;
    std::optional<i18n::ReviewStatus> requestedReview;
};
bool validLanguageTag(const std::string &);
bool layoutTemplate(const TerminalMessageRequest &,const Templates &,const TerminalTextPolicy &,TerminalMessageLine *);
bool wrapTemplate(const TerminalMessageRequest &,const Templates &,const TerminalTextPolicy &,std::size_t,TerminalMessageRows *);
bool renderTemplate(const TerminalMessageRequest &,const Templates &,const TerminalTextPolicy &,ColorLevel,PaintedTerminalMessageLine *);
bool renderWrappedTemplate(const TerminalMessageRequest &,const Templates &,const TerminalTextPolicy &,std::size_t,ColorLevel,PaintedTerminalMessageRows *);
} } // namespace om::terminal_message_detail
