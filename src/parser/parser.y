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
#include <iostream>
#include <cstdlib>
#include <memory>
#include <stdexcept>

#include "ast/generate/generate.h"
#include "ast/node.h"
#include "ast/type.h"
// 注意：本文件与所有包含 parser.hpp 的编译单元中，
// "ast/node.h"（进而 ast/type.h，内含 TypeId 枚举）必须先于
// "parser.hpp" 包含——bison 生成的关键字宏（CHAR/SHORT/LONG 等）
// 会破坏枚举成员名。
#include "parser.hpp"
extern int yydebug;

// 纯(pure)解析器 + 可重入 flex：yyparse/yylex 的全部状态经参数传递，
// 编译可以完全并发（不再需要进程级 parse 锁）
extern int yylex(YYSTYPE *yylval, YYLTYPE *yylloc, void *yyscanner);
extern int yyget_lineno(void *yyscanner);

// bison 的语义值联合只能存裸指针；AST 节点的子节点以 shared_ptr 持有，
// 动作里用 adopt() 将裸指针收进智能指针（构造即接管所有权）。
template <class T>
static std::shared_ptr<T> adopt(T* p) {
  return std::shared_ptr<T>(p);
}

// 节点构造一律经 mknode/mkshared：按产生式位置（@$）写入 line/column
//（BaseNode 构造入参的可选位置由这里统一负责）
template <class T, class... A>
static T* mknode(const YYLTYPE& loc, A&&... a) {
  T* n = new T(std::forward<A>(a)...);
  n->line = loc.first_line;
  n->column = loc.first_column;
  return n;
}
template <class T, class... A>
static std::shared_ptr<T> mkshared(const YYLTYPE& loc, A&&... a) {
  return std::shared_ptr<T>(mknode<T>(loc, std::forward<A>(a)...));
}

// 词法错误入口（tokens.l 调用）：没有 yylloc 可用，用 yyget_lineno。
// 输入文件名由 Parser::parse 在捕获异常后补充
void yylex_error(void *scanner, const char *s) {
    std::cerr << yyget_lineno(scanner) << ": error: " << s << std::endl;
    // 抛出异常而非 exit：与进程内多线程使用方式兼容，
    // scanner 的销毁由 Parser::parse 的 catch 分支完成
    throw std::runtime_error(std::string(s) + " at line " +
                             std::to_string(yyget_lineno(scanner)));
}

// bison 语法错误入口（parse-param 会按声明顺序追加到调用里）
void yyerror(YYLTYPE *loc, syc::ast::Parser &parser, void *scanner,
             const char *s) {
    (void)parser;
    (void)scanner;
    std::cerr << loc->first_line << ':' << loc->first_column << ": error: "
              << s << std::endl;
    throw std::runtime_error(std::string(s) + " at line " +
                             std::to_string(loc->first_line) + ":" +
                             std::to_string(loc->first_column));
}
#define YYERROR_VERBOSE true

// ── 类型说明符序列的累积器（参考 C 语言组合规则）──────────────────────────
struct TypeSpecAcc {
    bool has_char = false;
    bool has_short = false;
    bool has_int = false;
    bool has_void = false;
    bool has_float = false;
    bool has_double = false;
    bool has_bool = false;
    int n_long = 0;
    int sign = 0;  // 0 未指定，+1 signed，-1 unsigned
};

static void typespec_apply(TypeSpecAcc& a, int tok);
static syc::ast::types::TypeInfo typespec_finalize(const TypeSpecAcc& a);

static void typespec_apply(TypeSpecAcc& a, int tok) {
    switch (tok) {
        case SIGNED:
            if (a.sign == -1) throw std::runtime_error("duplicate 'signed'/'unsigned' in type specifier");
            a.sign = 1; break;
        case UNSIGNED:
            if (a.sign == 1) throw std::runtime_error("duplicate 'signed'/'unsigned' in type specifier");
            a.sign = -1; break;
        case CHAR:  a.has_char = true; break;
        case SHORT: a.has_short = true; break;
        case LONG:  a.n_long++; break;
        case INT:   a.has_int = true; break;
        case VOID:  a.has_void = true; break;
        case FLOAT: a.has_float = true; break;
        case DOUBLE: a.has_double = true; break;
        case BOOL:  a.has_bool = true; break;
        default: break;
    }
}

static syc::ast::types::TypeInfo typespec_finalize(const TypeSpecAcc& a) {
    using syc::ast::types::TypeId;
    TypeId t;
    if (a.has_void) {
        if (a.has_char || a.has_short || a.has_int || a.n_long || a.sign ||
            a.has_float || a.has_double || a.has_bool)
            throw std::runtime_error("'void' cannot be combined with other type specifiers");
        t = TypeId::VOID;
    } else if (a.has_bool) {
        if (a.has_char || a.has_short || a.has_int || a.n_long || a.sign ||
            a.has_float || a.has_double)
            throw std::runtime_error("'bool' cannot be combined with other type specifiers");
        t = TypeId::BOOL;
    } else if (a.has_float || a.has_double) {
        if (a.has_char || a.has_short || a.has_int || a.sign)
            throw std::runtime_error("'float'/'double' cannot be combined with integer type specifiers");
        if (a.has_float && a.has_double)
            throw std::runtime_error("invalid combination of 'float' and 'double'");
        // long double 按 double 处理（本平台无扩展精度浮点）
        t = a.has_double ? TypeId::DOUBLE : TypeId::FLOAT;
    } else if (a.has_char) {
        if (a.has_short || a.has_int || a.n_long)
            throw std::runtime_error("invalid combination of type specifiers");
        t = a.sign > 0 ? TypeId::SCHAR : (a.sign < 0 ? TypeId::UCHAR : TypeId::CHAR);
    } else if (a.has_short) {
        if (a.n_long) throw std::runtime_error("'short' cannot be combined with 'long'");
        t = a.sign < 0 ? TypeId::USHORT : TypeId::SHORT;
    } else if (a.n_long >= 2) {
        t = a.sign < 0 ? TypeId::ULLONG : TypeId::LLONG;
    } else if (a.n_long == 1) {
        t = a.sign < 0 ? TypeId::ULONG : TypeId::LONG;
    } else {
        t = a.sign < 0 ? TypeId::UINT : TypeId::INT;
    }
    return syc::ast::types::TypeInfo{t};
}
#ifdef YYDEBUG
#undef YYDEBUG
#endif
#define YYDEBUG 1
#define YYLLOC_DEFAULT(Current, Rhs, N)                               \
    do {                                                              \
        if (N) {                                                      \
            (Current).first_line   = YYRHSLOC (Rhs, 1).first_line;    \
            (Current).first_column = YYRHSLOC (Rhs, 1).first_column;  \
            (Current).last_line    = YYRHSLOC (Rhs, N).last_line;     \
            (Current).last_column  = YYRHSLOC (Rhs, N).last_column;   \
        } else {                                                      \
            (Current).first_line   = (Current).last_line   =          \
              YYRHSLOC (Rhs, 0).last_line;                            \
            (Current).first_column = (Current).last_column =          \
              YYRHSLOC (Rhs, 0).last_column;                          \
        }                                                             \
        yylloc = Current;                                             \
    } while (0)
#define yytnamerr(_yyres, _yystr)                                         \
        ([](char* yyres, const char* yystr) {                             \
            if (*yystr == '"') {                                          \
                if (yyres) return yystpcpy(yyres, yystr + 1) - yyres - 1; \
                else return yystrlen(yystr) - 2;                          \
            } else {                                                      \
                if (yyres) return yystpcpy(yyres, yystr) - yyres;         \
                else return yystrlen(yystr);                              \
            }                                                             \
        })(_yyres, _yystr)
%}

%code requires {
  // 拷入 parser.hpp：yyparse 的 parse-param 需要 Parser 前置声明
  namespace syc::ast { class Parser; }
}

%locations
%define api.pure full
%parse-param { syc::ast::Parser &parser }
%parse-param { void *scanner }
%lex-param { void *scanner }
%union {
    YYSTYPE() : token(0) {}
    int token;
    syc::ast::types::TypeInfo typeinfo;
    struct TypeSpecAcc* tacc;
    syc::ast::node::Identifier* ident;
    syc::ast::node::Expression* expr;
    syc::ast::node::Root* root;
    syc::ast::node::DeclareStatement* declare_statement;
    syc::ast::node::FunctionDefine* fundef;
    syc::ast::node::Declare* declare;
    syc::ast::node::ArrayDeclareInitValue* array_init_value;
    syc::ast::node::ArrayIdentifier* array_identifier;
    syc::ast::node::FunctionCallArgList* arg_list;
    syc::ast::node::FunctionDefineArgList* fundefarglist;
    syc::ast::node::FunctionDefineArg* fundefarg;
    syc::ast::node::Block* block;
    syc::ast::node::Statement* stmt;
    syc::ast::node::Assignment* assignmentstmt;
    syc::ast::node::IfElseStatement* ifelsestmt;
    syc::ast::node::ConditionExpression* condexp;
    std::string *string;
}

%token <string> INTEGER_VALUE "integer" IDENTIFIER "identifier" STRING_VALUE "string" FLOAT_VALUE "floating"
%token <token> IF "'if'" ELSE "'else'" WHILE "'while'" FOR "'for'"
%token <token> BREAK "'break'" CONTINUE "'continue'" RETURN "'return'"
%token <token> CONST "'const'" INT "'int'" VOID "'void'"
%token <token> SIGNED "'signed'" UNSIGNED "'unsigned'"
%token <token> CHAR "'char'" SHORT "'short'" LONG "'long'"
%token <token> FLOAT "'float'" DOUBLE "'double'" BOOL "'bool'"
%token <token> TRUE "'true'" FALSE "'false'"
%token <token> ASSIGN "'='" EQ "'=='" NE "'!='" LT "'<'" LE "'<='" GT  "'>'" GE "'>='"
%token <token> AND "'&&'" OR "'||'"
%token <token> LPAREN "'('" RPAREN "')'" LBRACE "'{'" RBRACE "'}'" LSQUARE "'['" RSQUARE "']'"

%token <token> DOT "'.'" COMMA "','" SEMI "';'"
%token <token> PLUSPLUS "'++'" MINUSMINUS "'--'"
%token <token> PLUS "'+'" MINUS "'-'" MUL "'*'" DIV "'/'" MOD "'%'" NOT "'!'"

%left COMMA
%left ASSIGN
%left EQ NE LT LE GT GE
%left PLUS MINUS
%left MUL DIV MOD
%left NOT

%type <token> AddOp MulOp RelOp UnaryOp
%type <tacc> TypeSpecs
%type <ident> ident LVal
%type <expr> Number Exp CommaExpr LOrExp LAndExp EqExp AddExp MulExp PrimaryExp RelExp UnaryExp FunctionCall
%type <root> CompUnit
%type <declare_statement> Decl ConstDecl VarDecl ConstDeclStmt VarDeclStmt
%type <declare> Def DefOne DefArray ConstDef ConstDefOne ConstDefArray
%type <array_identifier> DefArrayName ArrayItem
%type <fundef> FuncDef
%type <fundefarglist> FuncFParams
%type <fundefarg> FuncFParam FuncFParamArray FuncFParamOne
%type <array_init_value> InitValArray InitValArrayInner
%type <arg_list> FuncRParams
%type <expr> FuncRParam
%type <block> Block BlockItems
%type <stmt> BlockItem Stmt AssignStmt AssignStmtWithoutSemi IfStmt ReturnStmt WhileStmt ForStmt BreakStmt ContinueStmt
%type <condexp> Cond

%start CompUnit
%%

CompUnit: CompUnit Decl { $$->body.push_back(adopt($<declare>2)); }
        | CompUnit FuncDef { $$->body.push_back(adopt($<fundef>2)); }
        | Decl { parser.root = mknode<syc::ast::node::Root>(@$); $$ = parser.root; $$->body.push_back(adopt($<declare>1)); }
        | FuncDef { parser.root = mknode<syc::ast::node::Root>(@$); $$ = parser.root; $$->body.push_back(adopt($<fundef>1)); }
        ;

Decl: ConstDeclStmt
    | VarDeclStmt
    ;

// 类型说明符序列（参考 C：组合 signed/unsigned/char/short/int/long）
PrimSpec: SIGNED
        | UNSIGNED
        | CHAR
        | SHORT
        | LONG
        | INT
        | VOID
        | FLOAT
        | DOUBLE
        | BOOL
        ;

TypeSpecs: PrimSpec {
               $$ = new TypeSpecAcc();
               typespec_apply(*$$, $<token>1);
           }
         | TypeSpecs PrimSpec {
               typespec_apply(*$<tacc>1, $<token>2);
               $$ = $<tacc>1;
           }
         ;

// 所有声明点直接消费 TypeSpecs 累积器（指针），在动作中完成合法性检查。
// 不能为类型序列引入带类型的中间非终结符：它会使 BType 的归约与
// 声明/函数定义共享前缀时的移进产生无法恢复的冲突。

ConstDeclStmt: ConstDecl SEMI { $$ = $1; };

ConstDecl: CONST TypeSpecs ConstDef {
               $$ = mknode<syc::ast::node::DeclareStatement>(@$, 
                   typespec_finalize(*$<tacc>2));
               delete $<tacc>2;
               $$->list.push_back(adopt($3));
           }
         | ConstDecl COMMA ConstDef { $$->list.push_back(adopt($3)); }
         ;

VarDeclStmt: VarDecl SEMI { $$ = $1; };

VarDecl: TypeSpecs Def {
             $$ = mknode<syc::ast::node::DeclareStatement>(@$, 
                 typespec_finalize(*$<tacc>1));
             delete $<tacc>1;
             $$->list.push_back(adopt($2));
         }
       | VarDecl COMMA Def { $$->list.push_back(adopt($3)); }
       ;

Def: DefOne
   | DefArray
   ;

DefOne: ident ASSIGN LOrExp { $$ = mknode<syc::ast::node::VarDeclareWithInit>(@$, adopt($1), adopt($3)); }
      | ident { $$ = mknode<syc::ast::node::VarDeclare>(@$, adopt($1)); }
      ;

DefArray: DefArrayName ASSIGN InitValArray { $$ = mknode<syc::ast::node::ArrayDeclareWithInit>(@$, adopt($1), adopt($3)); }
        | DefArrayName { $$ = mknode<syc::ast::node::ArrayDeclare>(@$, adopt($1)); }
        ;

ConstDef: ConstDefOne
        | ConstDefArray
        ;

ConstDefOne: ident ASSIGN LOrExp { $$ = mknode<syc::ast::node::VarDeclareWithInit>(@$, adopt($1), adopt($3), true); }
           ;

ConstDefArray: DefArrayName ASSIGN InitValArray { $$ = mknode<syc::ast::node::ArrayDeclareWithInit>(@$, adopt($1), adopt($3), true); }
             ;

DefArrayName: DefArrayName LSQUARE Exp RSQUARE { $$ = $1; $$->shape.push_back(adopt($3)); }
            | ident LSQUARE Exp RSQUARE { $$ = mknode<syc::ast::node::ArrayIdentifier>(@$, adopt($1)); $$->shape.push_back(adopt($3)); }
            ;

InitValArray: LBRACE InitValArrayInner RBRACE { $$ = $2; }
            | LBRACE RBRACE { $$ = mknode<syc::ast::node::ArrayDeclareInitValue>(@$, false, nullptr); }
            ;

InitValArrayInner: InitValArrayInner COMMA InitValArray { $$ = $1; $$->value_list.push_back(adopt($3)); }
                 | InitValArrayInner COMMA LOrExp { $$ = $1; $$->value_list.push_back(adopt(mknode<syc::ast::node::ArrayDeclareInitValue>(@$, true, adopt($3)))); }
                 | InitValArray { $$ = mknode<syc::ast::node::ArrayDeclareInitValue>(@$, false, nullptr); $$->value_list.push_back(adopt($1)); }
                 | LOrExp { $$ = mknode<syc::ast::node::ArrayDeclareInitValue>(@$, false, nullptr); $$->value_list.push_back(adopt(mknode<syc::ast::node::ArrayDeclareInitValue>(@$, true, adopt($1)))); }
                 | LOrExp COMMA InitValArray  {
                       $$ = mknode<syc::ast::node::ArrayDeclareInitValue>(@$, false, nullptr);
                       $$->value_list.push_back(adopt(mknode<syc::ast::node::ArrayDeclareInitValue>(@$, true, adopt($1))));
                       $$->value_list.push_back(adopt($3));
                  }
                 | CommaExpr {
                       $$ = mknode<syc::ast::node::ArrayDeclareInitValue>(@$, false, nullptr);
                       for (auto& i : dynamic_cast<syc::ast::node::CommaExpression*>($1)->values) {
                             $$->value_list.push_back(adopt(mknode<syc::ast::node::ArrayDeclareInitValue>(@$, true, i)));
                       }
                       delete $1;
                 }
                 | CommaExpr COMMA InitValArray {
                       $$ = mknode<syc::ast::node::ArrayDeclareInitValue>(@$, false, nullptr);
                       for (auto& i : dynamic_cast<syc::ast::node::CommaExpression*>($1)->values) {
                             $$->value_list.push_back(adopt(mknode<syc::ast::node::ArrayDeclareInitValue>(@$, true, i)));
                       }
                       $$->value_list.push_back(adopt($3));
                       delete $1;
                 }
                 ;

Exp: LOrExp
   | CommaExpr
   ;

CommaExpr: LOrExp COMMA LOrExp {
               auto n = mknode<syc::ast::node::CommaExpression>(@$);
               n->values.push_back(adopt($1));
               n->values.push_back(adopt($3));
               $$ = n;
            }
         | CommaExpr COMMA LOrExp {
               dynamic_cast<syc::ast::node::CommaExpression*>($1)->values.push_back(adopt($3));
               $$ = $1;
            }
         ;

LOrExp: LAndExp OR LAndExp { $$ = mknode<syc::ast::node::BinaryExpression>(@$, adopt($1), $2, adopt($3)); }
      | LOrExp OR LAndExp { $$ = mknode<syc::ast::node::BinaryExpression>(@$, adopt($1), $2, adopt($3)); }
      | LAndExp
      ;

LAndExp: LAndExp AND EqExp  { $$ = mknode<syc::ast::node::BinaryExpression>(@$, adopt($1), $2, adopt($3)); }
       | EqExp
       ;

EqExp: RelExp EQ RelExp  { $$ = mknode<syc::ast::node::BinaryExpression>(@$, adopt($1), $2, adopt($3)); }
     | RelExp NE RelExp  { $$ = mknode<syc::ast::node::BinaryExpression>(@$, adopt($1), $2, adopt($3)); }
     | RelExp
     ;

RelExp: AddExp
      | RelExp RelOp AddExp  { $$ = mknode<syc::ast::node::BinaryExpression>(@$, adopt($1), $2, adopt($3)); }
      ;

AddExp: AddExp AddOp MulExp  { $$ = mknode<syc::ast::node::BinaryExpression>(@$, adopt($1), $2, adopt($3)); }
      | MulExp
      ;

MulExp: MulExp MulOp UnaryExp  { $$ = mknode<syc::ast::node::BinaryExpression>(@$, adopt($1), $2, adopt($3)); }
      | UnaryExp
      ;

UnaryExp: UnaryOp UnaryExp  { $$ = mknode<syc::ast::node::UnaryExpression>(@$, $1, adopt($2)); }
        | FunctionCall
        | PrimaryExp
        ;

FunctionCall: ident LPAREN FuncRParams RPAREN { $$ = mknode<syc::ast::node::FunctionCall>(@$, adopt($1), adopt($3)); };
            | ident LPAREN RPAREN { $$ = mknode<syc::ast::node::FunctionCall>(@$, adopt($1), mkshared<syc::ast::node::FunctionCallArgList>(@$)); }
            ;

PrimaryExp: LVal
          | Number
          | LPAREN LOrExp RPAREN { $$ = $2; }
          | AssignStmtWithoutSemi
          ;

ArrayItem: LVal LSQUARE Exp RSQUARE { $$ = mknode<syc::ast::node::ArrayIdentifier>(@$, adopt($1)); $$->shape.push_back(adopt($3));}
         | ArrayItem LSQUARE Exp RSQUARE { $$ = $1; $$->shape.push_back(adopt($3));}
         ;

LVal: ArrayItem
    | ident
    ;

FuncDef: TypeSpecs ident LPAREN FuncFParams RPAREN Block
         { $$ = mknode<syc::ast::node::FunctionDefine>(@$, 
               typespec_finalize(*$<tacc>1), adopt($2), adopt($4), adopt($6));
           delete $<tacc>1; }
       | TypeSpecs ident LPAREN RPAREN Block
         { $$ = mknode<syc::ast::node::FunctionDefine>(@$, 
               typespec_finalize(*$<tacc>1), adopt($2),
               mkshared<syc::ast::node::FunctionDefineArgList>(@$), adopt($5));
           delete $<tacc>1; }
       ;
       ;


FuncFParams: FuncFParams COMMA FuncFParam { $$->list.push_back(adopt($3)); }
           | FuncFParam {{ $$ = mknode<syc::ast::node::FunctionDefineArgList>(@$); $$->list.push_back(adopt($1)); }}
           ;

FuncFParam: FuncFParamOne
          | FuncFParamArray
          ;

FuncRParams: FuncRParams COMMA FuncRParam { $$ = $1; $$->args.push_back(adopt($3)); }
           | FuncRParam { $$ = mknode<syc::ast::node::FunctionCallArgList>(@$); $$->args.push_back(adopt($1)); }
           ;

FuncRParam: LOrExp
          | STRING_VALUE { $$ = mknode<syc::ast::node::StringLiteral>(@$, *$1); }
          ;

FuncFParamOne: TypeSpecs ident {
                   $$ = mknode<syc::ast::node::FunctionDefineArg>(@$, 
                       typespec_finalize(*$<tacc>1), adopt($2));
                   delete $<tacc>1;
             };

FuncFParamArray: FuncFParamOne LSQUARE RSQUARE {
                        $$ = mknode<syc::ast::node::FunctionDefineArg>(@$, 
                              $1->ty,
                              mkshared<syc::ast::node::ArrayIdentifier>(@$, 
                                  mkshared<syc::ast::node::ArrayIdentifier>(@$, 
                                      mkshared<syc::ast::node::Identifier>(@$, $1->name->name))));
                        std::dynamic_pointer_cast<syc::ast::node::ArrayIdentifier>($$->name)
                            ->shape.push_back(mkshared<syc::ast::node::Number>(@$, 1));
                        delete $1;
                  }
               | FuncFParamArray LSQUARE Exp RSQUARE {
                        $$ = $1;
                        std::dynamic_pointer_cast<syc::ast::node::ArrayIdentifier>($$->name)
                            ->shape.push_back(adopt($3));
                  }
               ;

Block: LBRACE RBRACE { $$ = mknode<syc::ast::node::Block>(@$); }
     | LBRACE BlockItems RBRACE { $$ = $2; }
     ;

BlockItems: BlockItem { $$ = mknode<syc::ast::node::Block>(@$); $$->statements.push_back(adopt($1)); }
          | BlockItems BlockItem { $$ = $1; $$->statements.push_back(adopt($2)); }
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
    | Exp SEMI { $$ = new  syc::ast::node::EvalStatement(adopt($1)); }
    | SEMI { $$ = mknode<syc::ast::node::VoidStatement>(@$); }
    ;

AssignStmt: AssignStmtWithoutSemi SEMI { $$ = $1; }

AssignStmtWithoutSemi: LVal ASSIGN LOrExp { $$ = mknode<syc::ast::node::Assignment>(@$, adopt($1), adopt($3)); }
                     | PLUSPLUS LVal { auto l = adopt($2); $$ = mknode<syc::ast::node::Assignment>(@$, l, mkshared<syc::ast::node::BinaryExpression>(@$, l, PLUS, mkshared<syc::ast::node::Number>(@$, 1))); }
                     | MINUSMINUS LVal { auto l = adopt($2); $$ = mknode<syc::ast::node::Assignment>(@$, l, mkshared<syc::ast::node::BinaryExpression>(@$, l, MINUS, mkshared<syc::ast::node::Number>(@$, 1))); }
                     | LVal PLUSPLUS { $$ = mknode<syc::ast::node::AfterInc>(@$, adopt($1), PLUS); }
                     | LVal MINUSMINUS { $$ = mknode<syc::ast::node::AfterInc>(@$, adopt($1), MINUS); }
                     ;

IfStmt: IF LPAREN Cond RPAREN Stmt { $$ = mknode<syc::ast::node::IfElseStatement>(@$, adopt($3), adopt($5), mkshared<syc::ast::node::VoidStatement>(@$)); }
      | IF LPAREN Cond RPAREN Stmt ELSE Stmt { $$ = mknode<syc::ast::node::IfElseStatement>(@$, adopt($3), adopt($5), adopt($7)); }
      ;

ReturnStmt: RETURN Exp SEMI { $$ = mknode<syc::ast::node::ReturnStatement>(@$, adopt($2)); }
          | RETURN SEMI { $$ = mknode<syc::ast::node::ReturnStatement>(@$); }
          ;

WhileStmt: WHILE LPAREN Cond RPAREN Stmt { $$ = mknode<syc::ast::node::WhileStatement>(@$, adopt($3), adopt($5));};

ForStmt: FOR LPAREN BlockItem Cond SEMI Exp RPAREN Stmt {
                  auto b = mknode<syc::ast::node::Block>(@$);
                  auto do_block = mknode<syc::ast::node::Block>(@$);
                  do_block->statements.push_back(adopt($8));
                  do_block->statements.push_back(adopt(mknode<syc::ast::node::EvalStatement>(@$, adopt($6))));
                  b->statements.push_back(adopt($3));
                  b->statements.push_back(adopt(mknode<syc::ast::node::WhileStatement>(@$, adopt($4), adopt(do_block))));
                  $$ = b;
            }
       | FOR LPAREN BlockItem Cond SEMI AssignStmtWithoutSemi RPAREN Stmt {
                  auto b = mknode<syc::ast::node::Block>(@$);
                  auto do_block = mknode<syc::ast::node::Block>(@$);
                  do_block->statements.push_back(adopt($8));
                  do_block->statements.push_back(adopt($6));
                  b->statements.push_back(adopt($3));
                  b->statements.push_back(adopt(mknode<syc::ast::node::WhileStatement>(@$, adopt($4), adopt(do_block))));
                  $$ = b;
            }
       | FOR LPAREN BlockItem Cond SEMI RPAREN Stmt {
                  auto b = mknode<syc::ast::node::Block>(@$);
                  b->statements.push_back(adopt($3));
                  b->statements.push_back(adopt(mknode<syc::ast::node::WhileStatement>(@$, adopt($4), adopt($7))));
                  $$ = b;
            }
       ;

BreakStmt: BREAK SEMI { $$ = mknode<syc::ast::node::BreakStatement>(@$); };

ContinueStmt: CONTINUE SEMI { $$ = mknode<syc::ast::node::ContinueStatement>(@$); };

Cond: LOrExp;

Number: INTEGER_VALUE { $$ = mknode<syc::ast::node::Number>(@$, *$1); }
      | FLOAT_VALUE { $$ = mknode<syc::ast::node::Number>(@$, *$1); }
      | TRUE { $$ = mknode<syc::ast::node::Number>(@$, 1); }
      | FALSE { $$ = mknode<syc::ast::node::Number>(@$, 0); }
      ;

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

ident: IDENTIFIER { $$ = mknode<syc::ast::node::Identifier>(@$, *$1); }
	 ;
