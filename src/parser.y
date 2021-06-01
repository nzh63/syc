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
extern SYC::Node::Root *root; /* the top level root node of our final AST */
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
    SYC::Node::Identifier* ident;
    SYC::Node::Expression* expr;
    SYC::Node::Root* root;
    SYC::Node::DeclareStatement* declare_statement;
    SYC::Node::FunctionDefine* fundef;
    SYC::Node::Declare* declare;
    SYC::Node::ArrayDeclareInitValue* array_init_value;
    SYC::Node::ArrayIdentifier* array_identifier;
    SYC::Node::FunctionCallArgList* arg_list;
    SYC::Node::FunctionDefineArgList* fundefarglist;
    SYC::Node::FunctionDefineArg* fundefarg;
    SYC::Node::Block* block;
    SYC::Node::Statement* stmt;
    SYC::Node::Assignment* assignmentstmt;
    SYC::Node::IfElseStatement* ifelsestmt;
    SYC::Node::ConditionExpression* condexp;
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
        | Decl { root = new SYC::Node::Root(); $$ = root; $$->body.push_back($<declare>1); }
        | FuncDef { root = new SYC::Node::Root(); $$ = root; $$->body.push_back($<fundef>1); }
        ;

Decl: ConstDeclStmt
    | VarDeclStmt
    ;

BType: INT;

ConstDeclStmt: ConstDecl SEMI { $$ = $1; };

ConstDecl: CONST BType ConstDef { $$ = new SYC::Node::DeclareStatement($2); $$->list.push_back($3); }
         | ConstDecl COMMA ConstDef { $$->list.push_back($3); }
         ;

VarDeclStmt: VarDecl SEMI { $$ = $1; };

VarDecl: BType Def { $$ = new SYC::Node::DeclareStatement($1); $$->list.push_back($2); }
       | VarDecl COMMA Def { $$->list.push_back($3); }
       ;

Def: DefOne
   | DefArray
   ;

DefOne: ident ASSIGN InitVal { $$ = new SYC::Node::VarDeclareWithInit(*$1, *$3); }
      | ident { $$ = new SYC::Node::VarDeclare(*$1); }
      ;

DefArray: DefArrayName ASSIGN InitValArray { $$ = new SYC::Node::ArrayDeclareWithInit(*$1, *$3); }
        | DefArrayName { $$ = new SYC::Node::ArrayDeclare(*$1); }
        ;

ConstDef: ConstDefOne
        | ConstDefArray
        ;

ConstDefOne: ident ASSIGN InitVal { $$ = new SYC::Node::VarDeclareWithInit(*$1, *$3, true); }
           ;

ConstDefArray: DefArrayName ASSIGN InitValArray { $$ = new SYC::Node::ArrayDeclareWithInit(*$1, *$3, true); }
             ;

DefArrayName: DefArrayName LSQUARE Exp RSQUARE { $$ = $1; $$->shape.push_back($3); }
            | ident LSQUARE Exp RSQUARE { $$ = new SYC::Node::ArrayIdentifier(*$1); $$->shape.push_back($3); }
            ;

InitVal: AddExp;

InitValArray: LBRACE InitValArrayInner RBRACE { $$ = $2; }
            | LBRACE RBRACE { $$ = new SYC::Node::ArrayDeclareInitValue(false, nullptr); }
            ;

InitValArrayInner: InitValArrayInner COMMA InitValArray { $$ = $1; $$->value_list.push_back($3); }
                 | InitValArrayInner COMMA InitVal { $$ = $1; $$->value_list.push_back(new SYC::Node::ArrayDeclareInitValue(true, $3)); }
                 | InitValArray { $$ = new SYC::Node::ArrayDeclareInitValue(false, nullptr); $$->value_list.push_back($1); }
                 | InitVal { $$ = new SYC::Node::ArrayDeclareInitValue(false, nullptr); $$->value_list.push_back(new SYC::Node::ArrayDeclareInitValue(true, $1)); }
                 | CommaExpr {
                       $$ = new SYC::Node::ArrayDeclareInitValue(false, nullptr);
                       for (auto i : dynamic_cast<SYC::Node::CommaExpression*>($1)->values) {
                             $$->value_list.push_back(new SYC::Node::ArrayDeclareInitValue(true, i));
                       }
                       delete $1;
                 }
                 ;

Exp: AddExp
   | CommaExpr
   ;

CommaExpr: AddExp COMMA AddExp {
               auto n = new SYC::Node::CommaExpression();
               n->values.push_back($1);
               n->values.push_back($3);
               $$ = n;
            }
         | CommaExpr COMMA AddExp {
               dynamic_cast<SYC::Node::CommaExpression*>($1)->values.push_back($3);
               $$ = $1;
            }
         ;

LOrExp: LAndExp OR LAndExp { $$ = new SYC::Node::BinaryExpression(*$1, $2, *$3); }
      | LOrExp OR LAndExp { $$ = new SYC::Node::BinaryExpression(*$1, $2, *$3); }
      | LAndExp
      ;

LAndExp: LAndExp AND EqExp  { $$ = new SYC::Node::BinaryExpression(*$1, $2, *$3); }
       | EqExp
       ;

EqExp: RelExp EQ RelExp  { $$ = new SYC::Node::BinaryExpression(*$1, $2, *$3); }
     | RelExp NE RelExp  { $$ = new SYC::Node::BinaryExpression(*$1, $2, *$3); }
     | RelExp
     ;

RelExp: AddExp
      | RelExp RelOp AddExp  { $$ = new SYC::Node::BinaryExpression(*$1, $2, *$3); }
      ;

AddExp: AddExp AddOp MulExp  { $$ = new SYC::Node::BinaryExpression(*$1, $2, *$3); }
      | MulExp
      ;

MulExp: MulExp MulOp UnaryExp  { $$ = new SYC::Node::BinaryExpression(*$1, $2, *$3); }
      | UnaryExp
      ;

UnaryExp: UnaryOp UnaryExp  { $$ = new SYC::Node::UnaryExpression($1, *$2); }
        | FunctionCall
        | PrimaryExp
        ;

FunctionCall: ident LPAREN FuncRParams RPAREN { $$ = new SYC::Node::FunctionCall(*$1, *$3); };
            | ident LPAREN RPAREN { $$ = new SYC::Node::FunctionCall(*$1, *(new SYC::Node::FunctionCallArgList())); }
            ;

PrimaryExp: LVal
          | Number
          | LPAREN Cond RPAREN { $$ = $2; }
          | AssignStmtWithoutSemi
          ;

ArrayItem: LVal LSQUARE Exp RSQUARE { $$ = new SYC::Node::ArrayIdentifier(*$1); $$->shape.push_back($3);}
         | ArrayItem LSQUARE Exp RSQUARE { $$ = $1; $$->shape.push_back($3);}
         ;

LVal: ArrayItem
    | ident
    ;

FuncDef: BType ident LPAREN FuncFParams RPAREN Block { $$ = new SYC::Node::FunctionDefine($1, *$2, *$4, *$6); }
       | BType ident LPAREN RPAREN Block { $$ = new SYC::Node::FunctionDefine($1, *$2, *(new SYC::Node::FunctionDefineArgList()), *$5); }
       | VOID ident LPAREN FuncFParams RPAREN Block { $$ = new SYC::Node::FunctionDefine($1, *$2, *$4, *$6); }
       | VOID ident LPAREN RPAREN Block { $$ = new SYC::Node::FunctionDefine($1, *$2, *(new SYC::Node::FunctionDefineArgList()), *$5); }
       ;


FuncFParams: FuncFParams COMMA FuncFParam { $$->list.push_back($3); }
           | FuncFParam {{ $$ = new SYC::Node::FunctionDefineArgList(); $$->list.push_back($1); }}
           ;

FuncFParam: FuncFParamOne
          | FuncFParamArray
          ;

FuncRParams: FuncRParams COMMA AddExp { $$ = $1; $$->args.push_back($3); }
           | AddExp { $$ = new SYC::Node::FunctionCallArgList(); $$->args.push_back($1); }
           ;

FuncFParamOne: BType ident { $$ = new SYC::Node::FunctionDefineArg($1, *$2); };

FuncFParamArray: FuncFParamOne LSQUARE RSQUARE {
                        $$ = new SYC::Node::FunctionDefineArg(
                              $1->type,
                              *new SYC::Node::ArrayIdentifier(*(new SYC::Node::ArrayIdentifier($1->name))));
                        ((SYC::Node::ArrayIdentifier*)&($$->name))->shape.push_back(new SYC::Node::Number(1));
                        delete $1;
                  }
               | FuncFParamArray LSQUARE Exp RSQUARE { $$ = $1; ((SYC::Node::ArrayIdentifier*)&($$->name))->shape.push_back($3);; }
               ;

Block: LBRACE RBRACE { $$ = new SYC::Node::Block(); }
     | LBRACE BlockItems RBRACE { $$ = $2; }
     ;

BlockItems: BlockItem { $$ = new SYC::Node::Block(); $$->statements.push_back($1); }
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
    | Exp SEMI { $$ = new  SYC::Node::EvalStatement(*$1); }
    | SEMI { $$ = new SYC::Node::VoidStatement(); }
    ;

AssignStmt: AssignStmtWithoutSemi SEMI { $$ = $1; }

AssignStmtWithoutSemi: LVal ASSIGN AddExp { $$ = new SYC::Node::Assignment(*$1, *$3); }
                     | PLUSPLUS LVal { $$ = new SYC::Node::Assignment(*$2, *new SYC::Node::BinaryExpression(*$2, PLUS, *new SYC::Node::Number(1))); }
                     | MINUSMINUS LVal { $$ = new SYC::Node::Assignment(*$2, *new SYC::Node::BinaryExpression(*$2, MINUS, *new SYC::Node::Number(1))); }
                     | LVal PLUSPLUS { $$ = new SYC::Node::AfterInc(*$1, PLUS); }
                     | LVal MINUSMINUS { $$ = new SYC::Node::AfterInc(*$1, MINUS); }
                     ;

IfStmt: IF LPAREN Cond RPAREN Stmt { $$ = new SYC::Node::IfElseStatement(*$3, *$5, *new SYC::Node::VoidStatement()); }
      | IF LPAREN Cond RPAREN Stmt ELSE Stmt { $$ = new SYC::Node::IfElseStatement(*$3, *$5, *$7); }
      ;

ReturnStmt: RETURN Exp SEMI { $$ = new SYC::Node::ReturnStatement($2); }
          | RETURN SEMI { $$ = new SYC::Node::ReturnStatement(); }
          ;

WhileStmt: WHILE LPAREN Cond RPAREN Stmt { $$ = new SYC::Node::WhileStatement(*$3, *$5);};

ForStmt: FOR LPAREN BlockItem Cond SEMI Exp RPAREN Stmt {
                  auto b = new SYC::Node::Block();
                  auto do_block = new SYC::Node::Block();
                  do_block->statements.push_back($8);
                  do_block->statements.push_back(new SYC::Node::EvalStatement(*$6));
                  b->statements.push_back($3);
                  b->statements.push_back(new SYC::Node::WhileStatement(*$4, *do_block));
                  $$ = b;
            }
       | FOR LPAREN BlockItem Cond SEMI AssignStmtWithoutSemi RPAREN Stmt {
                  auto b = new SYC::Node::Block();
                  auto do_block = new SYC::Node::Block();
                  do_block->statements.push_back($8);
                  do_block->statements.push_back($6);
                  b->statements.push_back($3);
                  b->statements.push_back(new SYC::Node::WhileStatement(*$4, *do_block));
                  $$ = b;
            }
       | FOR LPAREN BlockItem Cond SEMI RPAREN Stmt {
                  auto b = new SYC::Node::Block();
                  auto do_block = $7;
                  b->statements.push_back($3);
                  b->statements.push_back(new SYC::Node::WhileStatement(*$4, *do_block));
                  $$ = b;
            }
       ;

BreakStmt: BREAK SEMI { $$ = new SYC::Node::BreakStatement(); };

ContinueStmt: CONTINUE SEMI { $$ = new SYC::Node::ContinueStatement(); };

Cond: LOrExp;

Number: INTEGER_VALUE { $$ = new SYC::Node::Number(*$1); };

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

ident: IDENTIFIER { $$ = new SYC::Node::Identifier(*$1); }
	 ;
