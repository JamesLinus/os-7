set -xe

yacc -d -r -t -v ckmatch2.y
flex ck.l
gcc -g -o ck_yacc y.tab.c y.code.c lex.yy.c
OLDPWD=`pwd`
cd $SRCROOT/os/lib/yy
make
cd $SRCROOT/os/apps/ck/lib/gram
make
cd test
$SRCROOT/$ARCH$VARIANT$DEBUG/obj/os/apps/ck/lib/gram/gramgen -v gram.c
gcc -g -o ck_min -I$SRCROOT/os/include gram.c testpar.c $SRCROOT/$ARCH$VARIANT$DEBUG/obj/os/lib/yy/build/yy.a $SRCROOT/$ARCH$VARIANT$DEBUG/obj/os/lib/rtl/rtlc/build/rtlc.a
