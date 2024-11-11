# Minibash

Minibash is a linux bash program written in c by Tanmay Damle. 
It can run any executables in it provided they give appropriate amount of arguments and special character rules mentioned below in special characters. It implements the usage of system calls to achieve all the functions. It uses fork(), exec(), dup(), dup2(), pipe() and signal() system calls to do various tasks. It mimics the behaviour of Bourne-Again SHell terminal.

## Flow of minibash

![Flow](https://github.com/damletanmay/minibash/blob/main/minibash.png)

## Usage
       minibash --help # show manual page
       minibash <bash_script> # to run multiple commands one after the another
       minibash # to enter into minibash

Take a look at the official manual page for [Minibash](https://github.com/damletanmay/minibash/blob/main/minibash_man_page.txt)

Also Take a look at [test_cases](https://github.com/damletanmay/minibash/blob/main/test_cases) to see usage 
