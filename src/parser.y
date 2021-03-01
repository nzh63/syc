/*
 * syc, a compiler for SysY
 * Copyright (C) 2020  nzh63, skywf21
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
#include "node.h"
#include <cstdio>
#include <cstdlib>
NRoot *root; /* the top level root node of our final AST */
extern int yydebug;

extern int yylex();
extern int yyget_lineno();
extern int yylex_destroy();
void yyerror(const char *s) { std::printf("Error(line: %d): %s\n", yyget_lineno(), s); yylex_destroy(); if (!yydebug) std::exit(1); }
#define YYERROR_VERBOSE true
#define YYDEBUG 1
%}

%union {
    int token;
    NIdentifier* ident;
    NExpression* expr;
    NRoot* root;
    NDeclareStatement* declare_statement;
    NFunctionDefine* fundef;
    NDeclare* declare;
    NArrayDeclareInitValue* array_init_value;
    NArrayIdentifier* array_identifier;
    NFunctionCallArgList* arg_list;
    NFunctionDefineArgList* fundefarglist;
    NFunctionDefineArg* fundefarg;
    NBlock* block;
    NStatement* stmt;
    NAssignment* assignmentstmt;
    NIfElseStatement* ifelsestmt;
    NConditionExpression* condexp;
    std::string *string;
}

%token <string> INTEGER_VALUE IDENTIFIER
%token <token> IF ELSE WHILE FOR BREAK CONTINUE RETURN
%token <token> CONST INT VOID
%token <token> ASSIGN EQ NE LT LE GT GE
%token <token> AND OR
%token <token> LPAREN RPAREN LBRACE RBRACE LSQUARE RSQUARE

%token <token> DOT COMMA SEMI
%token <token> PLUSPLUS MINUSMINUS
%token <token> PLUS MINUS MUL DIV MOD NOT

%type <token> AddOp MulOp RelOp UnaryOp BType
%type <ident> ident LVal
%type <expr> Number Exp CommaExpr InitVal LOrExp LAndExp EqExp AddExp MulExp PrimaryExp RelExp UnaryExp FunctionCall
%type <root> CompUnit
%type <declare_statement> Decl ConstDecl VarDecl ConstDeclStmt VarDeclStmt
%type <declare> Def DefOne DefArray ConstDef ConstDefOne ConstDefArray
%type <array_identifier> DefArrayName ArrayItem
%type <fundef> FuncDef
%type <fundefarglist> FuncFParams
%type <fundefarg> FuncFParam FuncFParamArray FuncFParamOne
%type <array_init_value> InitValArray InitValArrayInner
%type <arg_list> FuncRParams
%type <block> Block BlockItems
%type <stmt> BlockItem Stmt AssignStmt AssignStmtWithoutSemi IfStmt ReturnStmt WhileStmt ForStmt BreakStmt ContinueStmt
%type <condexp> Cond

%start CompUnit
%%

CompUnit: CompUnit Decl { $$->body.push_back($<declare>2); }
        | CompUnit FuncDef { $$->body.push_back($<fundef>2); }
        | Decl { root = new NRoot(); $$ = root; $$->body.push_back($<declare>1); }
        | FuncDef { root = new NRoot(); $$ = root; $$->body.push_back($<fundef>1); }
        ;

Decl: ConstDeclStmt
    | VarDeclStmt
    ;

BType: INT;

ConstDeclStmt: ConstDecl SEMI { $$ = $1; };

ConstDecl: CONST BType ConstDef { $$ = new NDeclareStatement($2); $$->list.push_back($3); }
         | ConstDecl COMMA ConstDef { $$->list.push_back($3); }
         ;

VarDeclStmt: VarDecl SEMI { $$ = $1; };

VarDecl: BType Def { $$ = new NDeclareStatement($1); $$->list.push_back($2); }
       | VarDecl COMMA Def { $$->list.push_back($3); }
       ;

Def: DefOne
   | DefArray
   ;

DefOne: ident ASSIGN InitVal { $$ = new NVarDeclareWithInit(*$1, *$3); }
      | ident { $$ = new NVarDeclare(*$1); }
      ;

DefArray: DefArrayName ASSIGN InitValArray { $$ = new NArrayDeclareWithInit(*$1, *$3); }
        | DefArrayName { $$ = new NArrayDeclare(*$1); }
        ;

ConstDef: ConstDefOne
        | ConstDefArray
        ;

ConstDefOne: ident ASSIGN InitVal { $$ = new NVarDeclareWithInit(*$1, *$3, true); }
           ;

ConstDefArray: DefArrayName ASSIGN InitValArray { $$ = new NArrayDeclareWithInit(*$1, *$3, true); }
             ;

DefArrayName: DefArrayName LSQUARE Exp RSQUARE { $$ = $1; $$->shape.push_back($3); }
            | ident LSQUARE Exp RSQUARE { $$ = new NArrayIdentifier(*$1); $$->shape.push_back($3); }
            ;

InitVal: AddExp;

InitValArray: LBRACE InitValArrayInner RBRACE { $$ = $2; }
            | LBRACE RBRACE { $$ = new NArrayDeclareInitValue(false, nullptr); }
            ;

InitValArrayInner: InitValArrayInner COMMA InitValArray { $$ = $1; $$->value_list.push_back($3); }
                 | InitValArrayInner COMMA InitVal { $$ = $1; $$->value_list.push_back(new NArrayDeclareInitValue(true, $3)); }
                 | InitValArray { $$ = new NArrayDeclareInitValue(false, nullptr); $$->value_list.push_back($1); }
                 | InitVal { $$ = new NArrayDeclareInitValue(false, nullptr); $$->value_list.push_back(new NArrayDeclareInitValue(true, $1)); }
                 | CommaExpr {
                       $$ = new NArrayDeclareInitValue(false, nullptr);
                       for (auto i : dynamic_cast<NCommaExpression*>($1)->values) {
                             $$->value_list.push_back(new NArrayDeclareInitValue(true, i));
                       }
                       delete $1;
                 }
                 ;

Exp: AddExp
   | CommaExpr
   ;

CommaExpr: AddExp COMMA AddExp {
               auto n = new NCommaExpression();
               n->values.push_back($1);
               n->values.push_back($3);
               $$ = n;
            }
         | CommaExpr COMMA AddExp {
               dynamic_cast<NCommaExpression*>($1)->values.push_back($3);
               $$ = $1;
            }
         ;

LOrExp: LAndExp OR LAndExp { $$ = new NBinaryExpression(*$1, $2, *$3); }
      | LOrExp OR LAndExp { $$ = new NBinaryExpression(*$1, $2, *$3); }
      | LAndExp
      ;

LAndExp: LAndExp AND EqExp  { $$ = new NBinaryExpression(*$1, $2, *$3); }
       | EqExp
       ;

EqExp: RelExp EQ RelExp  { $$ = new NBinaryExpression(*$1, $2, *$3); }
     | RelExp NE RelExp  { $$ = new NBinaryExpression(*$1, $2, *$3); }
     | RelExp
     ;

RelExp: AddExp
      | RelExp RelOp AddExp  { $$ = new NBinaryExpression(*$1, $2, *$3); }
      ;

AddExp: AddExp AddOp MulExp  { $$ = new NBinaryExpression(*$1, $2, *$3); }
      | MulExp
      ;

MulExp: MulExp MulOp UnaryExp  { $$ = new NBinaryExpression(*$1, $2, *$3); }
      | UnaryExp
      ;

UnaryExp: UnaryOp UnaryExp  { $$ = new NUnaryExpression($1, *$2); }
        | FunctionCall
        | PrimaryExp
        ;

FunctionCall: ident LPAREN FuncRParams RPAREN { $$ = new NFunctionCall(*$1, *$3); };
            | ident LPAREN RPAREN { $$ = new NFunctionCall(*$1, *(new NFunctionCallArgList())); }
            ;

PrimaryExp: LVal
          | Number
          | LPAREN Cond RPAREN { $$ = $2; }
          | AssignStmtWithoutSemi
          ;

ArrayItem: LVal LSQUARE Exp RSQUARE { $$ = new NArrayIdentifier(*$1); $$->shape.push_back($3);}
         | ArrayItem LSQUARE Exp RSQUARE { $$ = $1; $$->shape.push_back($3);}
         ;

LVal: ArrayItem
    | ident
    ;

FuncDef: BType ident LPAREN FuncFParams RPAREN Block { $$ = new NFunctionDefine($1, *$2, *$4, *$6); }
       | BType ident LPAREN RPAREN Block { $$ = new NFunctionDefine($1, *$2, *(new NFunctionDefineArgList()), *$5); }
       | VOID ident LPAREN FuncFParams RPAREN Block { $$ = new NFunctionDefine($1, *$2, *$4, *$6); }
       | VOID ident LPAREN RPAREN Block { $$ = new NFunctionDefine($1, *$2, *(new NFunctionDefineArgList()), *$5); }
       ;


FuncFParams: FuncFParams COMMA FuncFParam { $$->list.push_back($3); }
           | FuncFParam {{ $$ = new NFunctionDefineArgList(); $$->list.push_back($1); }}
           ;

FuncFParam: FuncFParamOne
          | FuncFParamArray
          ;

FuncRParams: FuncRParams COMMA AddExp { $$ = $1; $$->args.push_back($3); }
           | AddExp { $$ = new NFunctionCallArgList(); $$->args.push_back($1); }
           ;

FuncFParamOne: BType ident { $$ = new NFunctionDefineArg($1, *$2); };

FuncFParamArray: FuncFParamOne LSQUARE RSQUARE {
                        $$ = new NFunctionDefineArg(
                              $1->type,
                              *new NArrayIdentifier(*(new NArrayIdentifier($1->name))));
                        ((NArrayIdentifier*)&($$->name))->shape.push_back(new NNumber(1));
                        delete $1;
                  }
               | FuncFParamArray LSQUARE Exp RSQUARE { $$ = $1; ((NArrayIdentifier*)&($$->name))->shape.push_back($3);; }
               ;

Block: LBRACE RBRACE { $$ = new NBlock(); }
     | LBRACE BlockItems RBRACE { $$ = $2; }
     ;

BlockItems: BlockItem { $$ = new NBlock(); $$->statements.push_back($1); }
          | BlockItems BlockItem { $$ = $1; $$->statements.push_back($2); }
          ;

BlockItem: Decl
         | Stmt
         ;

Stmt: Block
    | AssignStmt
    | IfStmt
    | ReturnStmt
    | WhileStmt
    | ForStmt
    | BreakStmt
    | ContinueStmt
    | Exp SEMI { $$ = new  NEvalStatement(*$1); }
    | SEMI { $$ = new NVoidStatement(); }
    ;

AssignStmt: AssignStmtWithoutSemi SEMI { $$ = $1; }

AssignStmtWithoutSemi: LVal ASSIGN AddExp { $$ = new NAssignment(*$1, *$3); }
                     | PLUSPLUS LVal { $$ = new NAssignment(*$2, *new NBinaryExpression(*$2, PLUS, *new NNumber(1))); }
                     | MINUSMINUS LVal { $$ = new NAssignment(*$2, *new NBinaryExpression(*$2, MINUS, *new NNumber(1))); }
                     | LVal PLUSPLUS { $$ = new NAfterInc(*$1, PLUS); }
                     | LVal MINUSMINUS { $$ = new NAfterInc(*$1, MINUS); }
                     ;

IfStmt: IF LPAREN Cond RPAREN Stmt { $$ = new NIfStatement(*$3, *$5); }
      | IF LPAREN Cond RPAREN Stmt ELSE Stmt { $$ = new NIfElseStatement(*$3, *$5, *$7); }
      ;

ReturnStmt: RETURN Exp SEMI { $$ = new NReturnStatement($2); }
          | RETURN SEMI { $$ = new NReturnStatement(); }
          ;

WhileStmt: WHILE LPAREN Cond RPAREN Stmt { $$ = new NWhileStatement(*$3, *$5);};

ForStmt: FOR LPAREN BlockItem Cond SEMI Exp RPAREN Stmt {
                  auto b = new NBlock();
                  auto do_block = new NBlock();
                  do_block->statements.push_back($8);
                  do_block->statements.push_back(new NEvalStatement(*$6));
                  b->statements.push_back($3);
                  b->statements.push_back(new NWhileStatement(*$4, *do_block));
                  $$ = b;
            }
       | FOR LPAREN BlockItem Cond SEMI AssignStmtWithoutSemi RPAREN Stmt {
                  auto b = new NBlock();
                  auto do_block = new NBlock();
                  do_block->statements.push_back($8);
                  do_block->statements.push_back($6);
                  b->statements.push_back($3);
                  b->statements.push_back(new NWhileStatement(*$4, *do_block));
                  $$ = b;
            }
       | FOR LPAREN BlockItem Cond SEMI RPAREN Stmt {
                  auto b = new NBlock();
                  auto do_block = $7;
                  b->statements.push_back($3);
                  b->statements.push_back(new NWhileStatement(*$4, *do_block));
                  $$ = b;
            }
       ;

BreakStmt: BREAK SEMI { $$ = new NBreakStatement(); };

ContinueStmt: CONTINUE SEMI { $$ = new NContinueStatement(); };

Cond: LOrExp;

Number: INTEGER_VALUE { $$ = new NNumber(*$1); };

AddOp: PLUS
     | MINUS
     ;

MulOp: MUL
     | DIV
     | MOD
     ;

UnaryOp: PLUS
       | MINUS
       | NOT
       ;

RelOp: GT
     | GE
     | LT
     | LE
     ;

ident: IDENTIFIER { $$ = new NIdentifier(*$1); }
	 ;
