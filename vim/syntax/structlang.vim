" Vim syntax file
" Language: Structlang
" Maintainer: Daniel Golding
" Latest Revision: 2024 Apr 05

if exists("b:current_syntax")
  finish
endif

syn keyword structlangKeywords struct fn let new return break
syn keyword structlangKeywordsRepeat loop
syn keyword structlangKeywordsConditional if else

syn keyword structlangBuiltinTypes int void bool
syn keyword structlangConstants true false

"syn keyword structlangBuiltinFunctions print exit
syn keyword structlangTodo contained TODO XXX FIXME

syntax match structlangNumber "\v<\d+>"

syntax region structlangString start=/"/ skip=/\\"/ end=/"/

syntax region structlangComment start=+/\*+  end=+\*/+ contains=structlangTodo,@Spell
syntax region structlangCommentL start="//" end="$" keepend contains=structlangTodo,@Spell

highlight default link structlangComment Comment
highlight default link structlangCommentL Comment

highlight default link structlangNumber Number
highlight default link structlangString String

highlight default link structlangKeywords Keyword
highlight default link structlangKeywordsRepeat Repeat
highlight default link structlangKeywordsConditional Conditional
highlight default link structlangBuiltinTypes Type
highlight default link structlangConstants Constant
"highlight default link structlangBuiltinFunctions Function
highlight default link structlangTodo Todo
