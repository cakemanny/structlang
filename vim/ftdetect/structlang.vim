if has('nvim')
    " NeoVim doesn't use autocmds for each filetype in filetype.lua ..
    lua vim.filetype.add({ extension = { sl = 'structlang' }})
else
    " We use ! to unseat the default slang (S-Lang a.k.a SmallLisp)
    autocmd! BufRead,BufNewFile *.sl setfiletype structlang
endif
