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
#include <string>
#include "ast/node.h"
#include "parser.hpp"

void yyerror(const char *s);
int yycolumn = 1;

#define SAVE_TOKEN     yylval.string = new std::string(yytext, yyleng)
#define TOKEN(t)       (yylval.token = t)
#define YY_USER_ACTION yylloc.first_line = yylineno;            \
                       yylloc.first_column = yycolumn;          \
                       for (auto i : std::string(yytext_ptr)) { \
                           if (i == '\n') {                     \
                               yylineno++;                      \
                               yycolumn = 1;                    \
                           } else {                             \
                               yycolumn++;                      \
                           }                                    \
                       }                                        \
                       yylloc.last_line = yylineno;             \
                       yylloc.last_column = yycolumn - 1;
%}

%option noyywrap

%%

\/\/.*\n?                           ;
"/*"                        		{
                                        int c;
                                        while((c = yyinput()) != 0) {
                                            if(c == '\n') {
                                                yylineno++;
                                                yycolumn = 1;
                                            } else if(c == '*') {
                                                if((c = yyinput()) == '/')
                                                    break;
                                                else
                                                    unput(c);
                                                }
                                        }
                                    }
[ ]                                 ;
\n                                  ;
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
"putf"[ \t\n]*"("                   yylval.string = new std::string("printf"); *yy_cp = yy_hold_char; yy_hold_char='(';yy_cp--; yyleng--; yy_c_buf_p--; return IDENTIFIER;
"starttime"[ \t\n]*"("              yylval.string = new std::string("_sysy_starttime"); *yy_cp = yy_hold_char; yy_hold_char='(';yy_cp--; yyleng--; yy_c_buf_p--; *yy_c_buf_p='_';yy_c_buf_p--; *yy_c_buf_p='_';yy_c_buf_p--; *yy_c_buf_p='E';yy_c_buf_p--; *yy_c_buf_p='N';yy_c_buf_p--; *yy_c_buf_p='I';yy_c_buf_p--; *yy_c_buf_p='L';yy_c_buf_p--; *yy_c_buf_p='_';yy_c_buf_p--; *yy_c_buf_p='_';yy_c_buf_p--; return IDENTIFIER;
"stoptime"[ \t\n]*"("               yylval.string = new std::string("_sysy_stoptime"); *yy_cp = yy_hold_char; yy_hold_char='(';yy_cp--; yyleng--; yy_c_buf_p--; *yy_c_buf_p='_';yy_c_buf_p--; *yy_c_buf_p='_';yy_c_buf_p--; *yy_c_buf_p='E';yy_c_buf_p--; *yy_c_buf_p='N';yy_c_buf_p--; *yy_c_buf_p='I';yy_c_buf_p--; *yy_c_buf_p='L';yy_c_buf_p--; *yy_c_buf_p='_';yy_c_buf_p--; *yy_c_buf_p='_';yy_c_buf_p--; return IDENTIFIER;
"__LINE__"                          yylval.string = new std::string(std::to_string(yyget_lineno())); return INTEGER_VALUE;
"_SYSY_N"                           yylval.string = new std::string(std::to_string(1024)); return INTEGER_VALUE;
[a-zA-Z_][a-zA-Z0-9_]*              SAVE_TOKEN; return IDENTIFIER;
[0-9]+                              SAVE_TOKEN; return INTEGER_VALUE;
"0x"[0-9a-fA-F]+                    SAVE_TOKEN; return INTEGER_VALUE;

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

.                                   yyerror((std::string("Unknown token: ") + yytext).c_str()); yyterminate();

%%
