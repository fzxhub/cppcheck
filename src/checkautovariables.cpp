/*
 * Cppcheck - A tool for static C/C++ code analysis
 * Copyright (C) 2007-2009 Daniel Marjamäki and Cppcheck team.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/
 */

//---------------------------------------------------------------------------
// Auto variables checks
//---------------------------------------------------------------------------

#include "checkautovariables.h"

#include <algorithm>
#include <sstream>
#include <list>
#include <iostream>
#include <string>

//---------------------------------------------------------------------------


// Register this check class into cppcheck by creating a static instance of it..
namespace
{
static CheckAutoVariables instance;
}


// _callStack used when parsing into subfunctions.


bool CheckAutoVariables::errorAv(const Token* left, const Token* right)
{
    if (fp_list.find(left->str()) == fp_list.end())
    {
        return false;
    }

    return isAutoVar(right->varId());
}

bool CheckAutoVariables::isAutoVar(unsigned int varId)
{
    return (vd_list.find(varId) != vd_list.end());
}

bool CheckAutoVariables::isAutoVarArray(unsigned int varId)
{
    return (vda_list.find(varId) != vda_list.end());
}

void print(const Token *tok, int num)
{
    const Token *t = tok;
    std::cout << tok->linenr() << " PRINT ";
    for (int i = 0; i < num; i++)
    {
        std::cout << " [" << t->str() << "] ";
        t = t->next();
    }
    std::cout << std::endl;
}

bool isTypeName(const Token *tok)
{
    bool ret = false;
    std::string _str(tok->str());
    static const char * const type[] = {"case", "return", "delete", 0};
    for (int i = 0; type[i]; i++)
        ret |= (_str == type[i]);
    return !ret;
}

bool isExternOrStatic(const Token *tok)
{
    bool res = false;

    if (Token::Match(tok->tokAt(-1), "extern|static"))
        res = true;
    else if (Token::Match(tok->tokAt(-2), "extern|static"))
        res = true;
    else if (Token::Match(tok->tokAt(-3), "extern|static"))
        res = true;

    //std::cout << __PRETTY_FUNCTION__ << " " << tok->str() << " " << res << std::endl;
    return res;

}

void CheckAutoVariables::addVD(unsigned int varId)
{
    vd_list.insert(varId);
}

void CheckAutoVariables::addVDA(unsigned int varId)
{
    vda_list.insert(varId);
}

void CheckAutoVariables::autoVariables()
{
    bool begin_function = false;
    bool begin_function_decl = false;
    int bindent = 0;

    for (const Token *tok = _tokenizer->tokens(); tok; tok = tok->next())
    {

        if (Token::Match(tok, "%type% *|::| %var% ("))
        {
            begin_function = true;
            fp_list.clear();
            vd_list.clear();
            vda_list.clear();
        }
        else if (begin_function && begin_function_decl && Token::Match(tok, "%type% * * %var%"))
        {
            fp_list.insert(tok->tokAt(3)->str());
        }
        else if (begin_function && begin_function_decl && Token::Match(tok, "%type% * %var% ["))
        {
            fp_list.insert(tok->tokAt(2)->str());
        }
        else if (begin_function && tok->str() == "(")
            begin_function_decl = true;
        else if (begin_function && tok->str() == ")")
        {
            begin_function_decl = false;
        }
        else if (begin_function && tok->str() == "{")
            bindent++;
        else if (begin_function && tok->str() == "}")
        {
            bindent--;
        }
        else if (bindent > 0 && Token::Match(tok, "%type% :: %any%") && !isExternOrStatic(tok)) //Inside a function
        {
            addVD(tok->tokAt(2)->varId());
        }
        // Inside a function body
        else if (bindent > 0 && Token::Match(tok, "%type% %var% ["))
        {
            addVDA(tok->next()->varId());
        }
        else if (bindent > 0 && Token::Match(tok, "%var% %var% ;") && !isExternOrStatic(tok)) //Inside a function
        {
            if (!isTypeName(tok))
                continue;
            addVD(tok->next()->varId());
        }
        else if (bindent > 0 && Token::Match(tok, "const %var% %var% ;") && !isExternOrStatic(tok)) //Inside a function
        {
            if (!isTypeName(tok->tokAt(1)))
                continue;
            addVD(tok->tokAt(2)->varId());
        }
        else if (bindent > 0 && Token::Match(tok, "[;{}] %var% = & %var%")) //Critical assignement
        {
            if (errorAv(tok->tokAt(1), tok->tokAt(4)))
                reportError(tok,
                            Severity::error,
                            "autoVariables",
                            "Wrong assignement of an auto-variable to an effective parameter of a function");
        }
        else if (bindent > 0 && Token::Match(tok, "[;{}] %var% [ %any% ] = & %var%")) //Critical assignement
        {
            if (errorAv(tok->tokAt(1), tok->tokAt(7)))
                reportError(tok,
                            Severity::error,
                            "autoVariables",
                            "Wrong assignement of an auto-variable to an effective parameter of a function");
        }
        else if (bindent > 0 && Token::Match(tok, "return & %var% ;")) //Critical return
        {
            if (isAutoVar(tok->tokAt(2)->varId()))
                reportError(tok,
                            Severity::error,
                            "autoVariables",
                            "Return of the address of an auto-variable");
        }
        // Invalid pointer deallocation
        else if (bindent > 0 && Token::Match(tok, "free ( %var% ) ;"))
        {
            if (isAutoVarArray(tok->tokAt(2)->varId()))
                reportError(tok,
                            Severity::error,
                            "autoVariables",
                            "Invalid deallocation");
        }
    }
    vd_list.clear();
    vda_list.clear();
    fp_list.clear();
}
//---------------------------------------------------------------------------



void CheckAutoVariables::returnPointerToLocalArray()
{
    bool infunc = false;
    int indentlevel = 0;
    std::list<unsigned int> arrayVar;
    for (const Token *tok = _tokenizer->tokens(); tok; tok = tok->next())
    {
        // Is there a function declaration for a function that returns a pointer?
        if (!infunc && (Token::Match(tok, "%type% * %var% (") || Token::Match(tok, "%type% * %var% :: %var% (")))
        {
            for (const Token *tok2 = tok; tok2; tok2 = tok2->next())
            {
                if (tok2->str() == ")")
                {
                    tok = tok2;
                    break;
                }
            }
            if (Token::simpleMatch(tok, ") {"))
            {
                infunc = true;
                indentlevel = 0;
                arrayVar.clear();
            }
        }

        // Parsing a function that returns a pointer..
        if (infunc)
        {
            if (tok->str() == "{")
                ++indentlevel;
            else if (tok->str() == "}")
            {
                --indentlevel;
                if (indentlevel <= 0)
                    infunc = false;
                continue;
            }

            // Declaring a local array..
            if (Token::Match(tok, "[;{}] %type% %var% ["))
            {
                arrayVar.push_back(tok->tokAt(2)->varId());
            }

            // Return pointer to local array variable..
            if (Token::Match(tok, "return %var% ;"))
            {
                unsigned int varid = tok->next()->varId();
                if (varid > 0 && std::find(arrayVar.begin(), arrayVar.end(), varid) != arrayVar.end())
                    errorReturnPointerToLocalArray(tok);
            }
        }

        // Declaring array variable..


    }
}

void CheckAutoVariables::errorReturnPointerToLocalArray(const Token *tok)
{
    reportError(tok, Severity::error, "returnLocalVariable", "Returning pointer to local array variable");
}



