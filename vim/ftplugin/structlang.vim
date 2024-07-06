" Vim filetype plugin file
" Language:	Structlang
" Maintainer:	Daniel Golding
" Last Change:	2024 Apr 05

if exists('b:did_ftplugin')
  finish
endif
let b:did_ftplugin = 1

setlocal formatoptions-=t

setlocal comments=s1:/*,mb:*,ex:*/,://
setlocal commentstring=//\ %s

" vim: sw=2 sts=2 et
