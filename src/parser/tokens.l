/*
 * syc, a compiler for SysY
 * Copyright (C) 2020-2021  nzh63, skywf21
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
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
%{
#include <cstring>
#include <string>
#include "ast/node.h"
#include "parser.hpp"

void yylex_error(void *scanner, const char *s);

// 可重入 flex：yylval/yylloc 是 yylex 的参数；列号存于 per-scanner 的
// yyextra（初值 1，由 yylex_init 的调用方设置）
#define SAVE_TOKEN     yylval_param->string = new std::string(yytext, yyleng)
#define TOKEN(t)       (yylval_param->token = t)
#define YY_USER_ACTION yylloc_param->first_line = yylineno;            \
                       yylloc_param->first_column = yyextra;           \
                       yylloc_param->last_line = yylineno;             \
                       yyextra += yyleng;                              \
                       yylloc_param->last_column = yyextra - 1;        \

// 把 "(__LINE__" 按逆序注回输入流（unput 是栈式回退），使后续输入变成
// "(__LINE__)"——对应 SysY 运行时宏 _sysy_starttime(__LINE__) 的展开。
// 必须是宏：unput 展开依赖 yylex 作用域内的 scanner 状态
#include <cstring>
#define INJECT_CALL_LINE()  do {                                                 \
                                const char *s = "(__LINE__";                     \
                                for (int i = (int)strlen(s) - 1; i >= 0; i--)    \
                                    unput(s[i]);                                 \
                            } while (0)
%}

%option noyywrap reentrant bison-bridge bison-locations yylineno
%option extra-type="int"

%%

\/\/[^\n]*                           ;
"/*"                        		{
                                        int c;
                                        while((c = yyinput(yyscanner)) != 0) {
                                            if(c == '\n') {
                                                yylineno++;
                                                yyextra = 1;
                                            } else if(c == '*') {
                                                if((c = yyinput(yyscanner)) == '/')
                                                    break;
                                                else
                                                    unput(c);
                                                }
                                        }
                                    }
[ ]                                 ;

\n                                  { yyextra = 1; }
[\t\r]                              ;
"if"                                return TOKEN(IF);
"else"                              return TOKEN(ELSE);
"while"                             return TOKEN(WHILE);
"for"                               return TOKEN(FOR);
"break"                             return TOKEN(BREAK);
"continue"                          return TOKEN(CONTINUE);
"return"                            return TOKEN(RETURN);
"const"                             return TOKEN(CONST);
"int"                               return TOKEN(INT);
"void"                              return TOKEN(VOID);
"signed"                            return TOKEN(SIGNED);
"unsigned"                          return TOKEN(UNSIGNED);
"char"                              return TOKEN(CHAR);
"short"                             return TOKEN(SHORT);
"long"                              return TOKEN(LONG);
"float"                             return TOKEN(FLOAT);
"double"                            return TOKEN(DOUBLE);
"bool"                              return TOKEN(BOOL);
"true"                              return TOKEN(TRUE);
"false"                             return TOKEN(FALSE);
"putf"[ \t\n]*"("                   yylval_param->string = new std::string("printf"); yyless(4); return IDENTIFIER;
"starttime"[ \t\n]*"("              yylval_param->string = new std::string("_sysy_starttime"); INJECT_CALL_LINE(); return IDENTIFIER;
"stoptime"[ \t\n]*"("               yylval_param->string = new std::string("_sysy_stoptime"); INJECT_CALL_LINE(); return IDENTIFIER;
"__LINE__"                          yylval_param->string = new std::string(std::to_string(yyget_lineno(yyscanner))); return INTEGER_VALUE;
"_SYSY_N"                           yylval_param->string = new std::string(std::to_string(1024)); return INTEGER_VALUE;
\"(\\.|[^"\\])*\"                   {
                                        std::string s;
                                        for (int i = 1; i < yyleng - 1; i++) {
                                            if (yytext[i] == '\\') {
                                                i++;
                                                switch (yytext[i]) {
                                                    case 'n':  s += '\n'; break;
                                                    case 't':  s += '\t'; break;
                                                    case 'r':  s += '\r'; break;
                                                    case '0':  s += '\0'; break;
                                                    case '\\': s += '\\'; break;
                                                    case '"':  s += '"';  break;
                                                    default:   s += yytext[i]; break;
                                                }
                                            } else {
                                                s += yytext[i];
                                            }
                                        }
                                        yylval_param->string = new std::string(s);
                                        return STRING_VALUE;
                                    }
[a-zA-Z_][a-zA-Z0-9_]*              SAVE_TOKEN; return IDENTIFIER;
[0-9]+\.[0-9]*([eE][+-]?[0-9]+)?[fFlL]?   SAVE_TOKEN; return FLOAT_VALUE;
\.[0-9]+([eE][+-]?[0-9]+)?[fFlL]?         SAVE_TOKEN; return FLOAT_VALUE;
[0-9]+[eE][+-]?[0-9]+[fFlL]?              SAVE_TOKEN; return FLOAT_VALUE;
[0-9]+[uUlL]{0,3}                   SAVE_TOKEN; return INTEGER_VALUE;
"0x"[0-9a-fA-F]+[uUlL]{0,3}         SAVE_TOKEN; return INTEGER_VALUE;
\'(\\.|[^\\\'\n])+\'                {
                                        // 字符字面量：解转义后按 GCC 约定大端打包为 int，
                                        // 以十进制字符串形式复用 INTEGER_VALUE 通路
                                        std::uint32_t packed = 0;
                                        for (int i = 1; i < yyleng - 1; i++) {
                                            char c = yytext[i];
                                            if (c == '\\') {
                                                i++;
                                                switch (yytext[i]) {
                                                    case 'n':  c = '\n'; break;
                                                    case 't':  c = '\t'; break;
                                                    case 'r':  c = '\r'; break;
                                                    case '0':  c = '\0'; break;
                                                    case '\\': c = '\\'; break;
                                                    case '\'': c = '\''; break;
                                                    case '"':  c = '"';  break;
                                                    default:   c = yytext[i]; break;
                                                }
                                            }
                                            packed = packed * 256 +
                                                     static_cast<unsigned char>(c);
                                        }
                                        yylval_param->string =
                                            new std::string(std::to_string(
                                                static_cast<std::int32_t>(packed)));
                                        return INTEGER_VALUE;
                                    }

"="                                 return TOKEN(ASSIGN);
"=="                                return TOKEN(EQ);
"!="                                return TOKEN(NE);
"<"                                 return TOKEN(LT);
"<="                                return TOKEN(LE);
">"                                 return TOKEN(GT);
">="                                return TOKEN(GE);
"&&"                                return TOKEN(AND);
"||"                                return TOKEN(OR);

"("                                 return TOKEN(LPAREN);
")"                                 return TOKEN(RPAREN);
"["                                 return TOKEN(LSQUARE);
"]"                                 return TOKEN(RSQUARE);
"{"                                 return TOKEN(LBRACE);
"}"                                 return TOKEN(RBRACE);

"."                                 return TOKEN(DOT);
","                                 return TOKEN(COMMA);
";"                                 return TOKEN(SEMI);

"++"                                return TOKEN(PLUSPLUS);
"--"                                return TOKEN(MINUSMINUS);

"+"                                 return TOKEN(PLUS);
"-"                                 return TOKEN(MINUS);
"*"                                 return TOKEN(MUL);
"/"                                 return TOKEN(DIV);
"%"                                 return TOKEN(MOD);
"!"                                 return TOKEN(NOT);

.                                   yylex_error(yyscanner, (std::string("Unknown token: ") + yytext).c_str()); yyterminate();

%%
