/* -*- C++ -*-
 *
 * template_text.h -- Class for manipulating template texts.
 *
 * Copyright (C) 2011, Computing Systems Laboratory (CSLab), NTUA.
 * Copyright (C) 2011, Vasileios Karakasis
 * All rights reserved.
 *
 * This file is distributed under the BSD License. See LICENSE.txt for details.
 */
#ifndef TEMPLATE_TEXT_H__
#define TEMPLATE_TEXT_H__

#include <map>
#include <string>
#include <boost/regex.hpp>

class TemplateText
{
public:
    TemplateText(std::string text);

    const std::string &GetTemplate() const
    {
        return const_cast<std::string&>(template_text_);
    }

    void SetTemplate(std::string text);

    std::string Substitute(const std::map<std::string, std::string> &values);

private:
    void ReplacePlaceholders();
    std::string DoSubstitute();
    std::string template_text_;
    std::map<std::string, std::string> placeholders_;
    boost::regex placeholder_pattern_;

    class TextReplacer {
    public:
        TextReplacer(TemplateText *outer)
            : outer_(outer) {}

        std::string operator()(
            const boost::match_results<std::string::iterator> &match)
        {
            std::string key(match[1].first, match[1].second);
            return outer_->placeholders_[key];
        }

    private:
        TemplateText *outer_;
    };
};

#endif  // TEMPLATE_TEXT_H__
