/*
    bit.h -- Build It Configuration Header for solaris-x86

    This header is generated by Bit during configuration.
    You may edit this file, but Bit will overwrite it next
    time configuration is performed.
 */

/* Settings */
#define BIT_BUILD_NUMBER "0"
#define BIT_COMPANY "Embedthis"
#define BIT_DEPTH 1
#define BIT_HAS_DOUBLE_BRACES 1
#define BIT_HAS_DYN_LOAD 1
#define BIT_HAS_LIB_EDIT 0
#define BIT_HAS_MMU 1
#define BIT_HAS_MTUNE 1
#define BIT_HAS_PAM 0
#define BIT_HAS_STACK_PROTECTOR 1
#define BIT_HAS_SYNC 0
#define BIT_HAS_SYNC_CAS 0
#define BIT_HAS_UNNAMED_UNIONS 1
#define BIT_MANAGER ""
#define BIT_MINIMAL "doxygen,dsi,man,man2html,pmaker,matrixssl,ssl"
#define BIT_OPTIONAL "doxygen,dsi,ejs,man,man2html,openssl,matrixssl,ssl,utest"
#define BIT_PAM 1
#define BIT_PRODUCT "http"
#define BIT_REQUIRED "compiler,link,pcre"
#define BIT_SYNC "mpr,pcre"
#define BIT_TITLE "Http Library"
#define BIT_VERSION "1.0.1"
#define BIT_WARN64TO32 0
#define BIT_WARN_UNUSED 0

/* Prefixes */
#define BIT_CFG_PREFIX "/etc/http"
#define BIT_BIN_PREFIX "/usr/lib/http/1.0.1/bin"
#define BIT_INC_PREFIX "/usr/lib/http/1.0.1/inc"
#define BIT_LOG_PREFIX "/var/log/http"
#define BIT_PRD_PREFIX "/usr/lib/http"
#define BIT_SPL_PREFIX "/var/spool/http"
#define BIT_SRC_PREFIX "/usr/src/http-1.0.1"
#define BIT_VER_PREFIX "/usr/lib/http/1.0.1"
#define BIT_WEB_PREFIX "/var/www/http-default"

/* Suffixes */
#define BIT_EXE ""
#define BIT_SHLIB ".so"
#define BIT_SHOBJ ".so"
#define BIT_LIB ".a"
#define BIT_OBJ ".o"

/* Profile */
#define BIT_HTTP_PRODUCT 1
#define BIT_PROFILE "debug"
#define BIT_CONFIG_CMD "bit -d -q -platform solaris-x86 -without all -configure . -gen sh,make"

/* Miscellaneous */
#define BIT_MAJOR_VERSION 1
#define BIT_MINOR_VERSION 0
#define BIT_PATCH_VERSION 1
#define BIT_VNUM 100000001

/* Packs */
#define BIT_PACK_CC 1
#define BIT_PACK_DOXYGEN 0
#define BIT_PACK_DSI 0
#define BIT_PACK_EJS 1
#define BIT_PACK_LINK 1
#define BIT_PACK_MAN 0
#define BIT_PACK_MAN2HTML 0
#define BIT_PACK_MATRIXSSL 0
#define BIT_PACK_OPENSSL 0
#define BIT_PACK_PCRE 1
#define BIT_PACK_PMAKER 0
#define BIT_PACK_SSL 0
#define BIT_PACK_UTEST 1
