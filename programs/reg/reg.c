/*
 * Copyright 2008 Andrew Riedi
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include <windows.h>
#include <wine/unicode.h>
#include "reg.h"

static int reg_printfW(const WCHAR *msg, ...)
{
    va_list va_args;
    int wlen;
    DWORD count, ret;
    WCHAR msg_buffer[8192];

    va_start(va_args, msg);
    vsprintfW(msg_buffer, msg, va_args);
    va_end(va_args);

    wlen = lstrlenW(msg_buffer);
    ret = WriteConsoleW(GetStdHandle(STD_OUTPUT_HANDLE), msg_buffer, wlen, &count, NULL);
    if (!ret)
    {
        DWORD len;
        char  *msgA;

        len = WideCharToMultiByte(GetConsoleOutputCP(), 0, msg_buffer, wlen,
            NULL, 0, NULL, NULL);
        msgA = HeapAlloc(GetProcessHeap(), 0, len * sizeof(char));
        if (!msgA)
            return 0;

        WideCharToMultiByte(GetConsoleOutputCP(), 0, msg_buffer, wlen, msgA, len,
            NULL, NULL);
        WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), msgA, len, &count, FALSE);
        HeapFree(GetProcessHeap(), 0, msgA);
    }

    return count;
}

static int reg_message(int msg)
{
    static const WCHAR formatW[] = {'%','s',0};
    WCHAR msg_buffer[8192];

    LoadStringW(GetModuleHandleW(NULL), msg, msg_buffer,
        sizeof(msg_buffer)/sizeof(WCHAR));
    return reg_printfW(formatW, msg_buffer);
}

static HKEY get_rootkey(LPWSTR key)
{
    static const WCHAR szHKLM[] = {'H','K','L','M',0};
    static const WCHAR szHKCU[] = {'H','K','C','U',0};
    static const WCHAR szHKCR[] = {'H','K','C','R',0};
    static const WCHAR szHKU[] = {'H','K','U',0};
    static const WCHAR szHKCC[] = {'H','K','C','C',0};

    if (CompareStringW(CP_ACP,NORM_IGNORECASE,key,4,szHKLM,4)==2)
        return HKEY_LOCAL_MACHINE;
    else if (CompareStringW(CP_ACP,NORM_IGNORECASE,key,4,szHKCU,4)==2)
        return HKEY_CURRENT_USER;
    else if (CompareStringW(CP_ACP,NORM_IGNORECASE,key,4,szHKCR,4)==2)
        return HKEY_CLASSES_ROOT;
    else if (CompareStringW(CP_ACP,NORM_IGNORECASE,key,3,szHKU,3)==2)
        return HKEY_USERS;
    else if (CompareStringW(CP_ACP,NORM_IGNORECASE,key,4,szHKCC,4)==2)
        return HKEY_CURRENT_CONFIG;
    else return NULL;
}

static DWORD get_regtype(LPWSTR type)
{
    static const WCHAR szREG_SZ[] = {'R','E','G','_','S','Z',0};
    static const WCHAR szREG_MULTI_SZ[] = {'R','E','G','_','M','U','L','T','I','_','S','Z',0};
    static const WCHAR szREG_DWORD_BIG_ENDIAN[] = {'R','E','G','_','D','W','O','R','D','_','B','I','G','_','E','N','D','I','A','N',0};
    static const WCHAR szREG_DWORD[] = {'R','E','G','_','D','W','O','R','D',0};
    static const WCHAR szREG_BINARY[] = {'R','E','G','_','B','I','N','A','R','Y',0};
    static const WCHAR szREG_DWORD_LITTLE_ENDIAN[] = {'R','E','G','_','D','W','O','R','D','_','L','I','T','T','L','E','_','E','N','D','I','A','N',0};
    static const WCHAR szREG_NONE[] = {'R','E','G','_','N','O','N','E',0};
    static const WCHAR szREG_EXPAND_SZ[] = {'R','E','G','_','E','X','P','A','N','D','_','S','Z',0};

    if (!type)
        return REG_SZ;

    if (lstrcmpiW(type,szREG_SZ)==0) return REG_SZ;
    if (lstrcmpiW(type,szREG_DWORD)==0) return REG_DWORD;
    if (lstrcmpiW(type,szREG_MULTI_SZ)==0) return REG_MULTI_SZ;
    if (lstrcmpiW(type,szREG_EXPAND_SZ)==0) return REG_EXPAND_SZ;
    if (lstrcmpiW(type,szREG_DWORD_BIG_ENDIAN)==0) return REG_DWORD_BIG_ENDIAN;
    if (lstrcmpiW(type,szREG_DWORD_LITTLE_ENDIAN)==0) return REG_DWORD_LITTLE_ENDIAN;
    if (lstrcmpiW(type,szREG_BINARY)==0) return REG_BINARY;
    if (lstrcmpiW(type,szREG_NONE)==0) return REG_NONE;

    return -1;
}

static LPBYTE get_regdata(LPWSTR data, DWORD reg_type, WCHAR separator, DWORD *reg_count)
{
    LPBYTE out_data = NULL;
    *reg_count = 0;

    switch (reg_type)
    {
        case REG_SZ:
        {
            *reg_count = (lstrlenW(data) + 1) * sizeof(WCHAR);
            out_data = HeapAlloc(GetProcessHeap(),0,*reg_count);
            lstrcpyW((LPWSTR)out_data,data);
            break;
        }
        case REG_DWORD:
        {
            LPWSTR rest;
            DWORD val;
            val = strtolW(data, &rest, 0);
            if (rest == data) {
                static const WCHAR nonnumber[] = {'E','r','r','o','r',':',' ','/','d',' ','r','e','q','u','i','r','e','s',' ','n','u','m','b','e','r','.','\n',0};
                reg_printfW(nonnumber);
                break;
            }
            *reg_count = sizeof(DWORD);
            out_data = HeapAlloc(GetProcessHeap(),0,*reg_count);
            ((LPDWORD)out_data)[0] = val;
            break;
        }
        default:
        {
            static const WCHAR unhandled[] = {'U','n','h','a','n','d','l','e','d',' ','T','y','p','e',' ','0','x','%','x',' ',' ','d','a','t','a',' ','%','s','\n',0};
            reg_printfW(unhandled, reg_type,data);
        }
    }

    return out_data;
}

static int reg_add(WCHAR *key_name, WCHAR *value_name, BOOL value_empty,
    WCHAR *type, WCHAR separator, WCHAR *data, BOOL force)
{
    static const WCHAR stubW[] = {'A','D','D',' ','-',' ','%','s',
        ' ','%','s',' ','%','d',' ','%','s',' ','%','s',' ','%','d','\n',0};
    LPWSTR p;
    HKEY root,subkey;

    reg_printfW(stubW, key_name, value_name, value_empty, type, data, force);

    if (key_name[0]=='\\' && key_name[1]=='\\')
    {
        reg_message(STRING_NO_REMOTE);
        return 1;
    }

    p = strchrW(key_name,'\\');
    if (!p)
    {
        reg_message(STRING_INVALID_KEY);
        return 1;
    }
    p++;

    root = get_rootkey(key_name);
    if (!root)
    {
        reg_message(STRING_INVALID_KEY);
        return 1;
    }

    if(RegCreateKeyW(root,p,&subkey)!=ERROR_SUCCESS)
    {
        reg_message(STRING_INVALID_KEY);
        return 1;
    }

    if (value_name || data)
    {
        DWORD reg_type;
        DWORD reg_count = 0;
        BYTE* reg_data = NULL;

        if (!force)
        {
            if (RegQueryValueW(subkey,value_name,NULL,NULL)==ERROR_SUCCESS)
            {
                /* FIXME:  Prompt for overwrite */
            }
        }

        reg_type = get_regtype(type);
        if (reg_type == -1)
        {
            RegCloseKey(subkey);
            reg_message(STRING_INVALID_CMDLINE);
            return 1;
        }

        if (data)
            reg_data = get_regdata(data,reg_type,separator,&reg_count);

        RegSetValueExW(subkey,value_name,0,reg_type,reg_data,reg_count);
        HeapFree(GetProcessHeap(),0,reg_data);
    }

    RegCloseKey(subkey);
    reg_message(STRING_SUCCESS);

    return 0;
}

static int reg_delete(WCHAR *key_name, WCHAR *value_name, BOOL value_empty,
    BOOL value_all, BOOL force)
{
    LPWSTR p;
    HKEY root,subkey;

    static const WCHAR stubW[] = {'D','E','L','E','T','E',
        ' ','-',' ','%','s',' ','%','s',' ','%','d',' ','%','d',' ','%','d','\n'
        ,0};
    reg_printfW(stubW, key_name, value_name, value_empty, value_all, force);

    if (key_name[0]=='\\' && key_name[1]=='\\')
    {
        reg_message(STRING_NO_REMOTE);
        return 1;
    }

    p = strchrW(key_name,'\\');
    if (!p)
    {
        reg_message(STRING_INVALID_KEY);
        return 1;
    }
    p++;

    root = get_rootkey(key_name);
    if (!root)
    {
        reg_message(STRING_INVALID_KEY);
        return 1;
    }

    if (value_name && value_empty)
    {
        reg_message(STRING_INVALID_CMDLINE);
        return 1;
    }

    if (value_empty && value_all)
    {
        reg_message(STRING_INVALID_CMDLINE);
        return 1;
    }

    if (!force)
    {
        /* FIXME:  Prompt for delete */
    }

    /* Delete subtree only if no /v* option is given */
    if (!value_name && !value_empty && !value_all)
    {
        if (RegDeleteTreeW(root,p)!=ERROR_SUCCESS)
        {
            reg_message(STRING_CANNOT_FIND);
            return 1;
        }
        reg_message(STRING_SUCCESS);
        return 0;
    }

    if(RegOpenKeyW(root,p,&subkey)!=ERROR_SUCCESS)
    {
        reg_message(STRING_CANNOT_FIND);
        return 1;
    }

    if (value_all)
    {
        LPWSTR szValue;
        DWORD maxValue;
        DWORD count;
        LONG rc;

        rc = RegQueryInfoKeyW(subkey, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
            &maxValue, NULL, NULL, NULL);
        if (rc != ERROR_SUCCESS)
        {
            /* FIXME: failure */
            RegCloseKey(subkey);
            return 1;
        }
        maxValue++;
        szValue = HeapAlloc(GetProcessHeap(),0,maxValue*sizeof(WCHAR));

        while (1)
        {
            count = maxValue;
            rc = RegEnumValueW(subkey, 0, szValue, &count, NULL, NULL, NULL, NULL);
            if (rc == ERROR_SUCCESS)
            {
                rc = RegDeleteValueW(subkey, szValue);
                if (rc != ERROR_SUCCESS)
                    break;
            }
            else break;
        }
        if (rc != ERROR_SUCCESS)
        {
            /* FIXME  delete failed */
        }
    }
    else if (value_name)
    {
        if (RegDeleteValueW(subkey,value_name) != ERROR_SUCCESS)
        {
            RegCloseKey(subkey);
            reg_message(STRING_CANNOT_FIND);
            return 1;
        }
    }
    else if (value_empty)
    {
        RegSetValueExW(subkey,NULL,0,REG_SZ,NULL,0);
    }

    RegCloseKey(subkey);
    reg_message(STRING_SUCCESS);
    return 0;
}

static const WCHAR HKCR_WSTR[] = {'H', 'K', 'E', 'Y', '_', 'C', 'L', 'A', 'S', 'S', 'E', 'S', '_', 'R', 'O', 'O', 'T',  0};
static DWORD HKCR_WSTR_SZ = sizeof(HKCR_WSTR);
static const WCHAR HKLM_WSTR[] = {'H', 'K', 'E', 'Y', '_', 'L', 'O', 'C', 'A', 'L', '_', 'M', 'A', 'C', 'H', 'I', 'N', 'E', 0};
static DWORD HKLM_WSTR_SZ = sizeof(HKLM_WSTR);
static const WCHAR HKCU_WSTR[] = {'H', 'K', 'E', 'Y', '_', 'C', 'U', 'R', 'R', 'E', 'N', 'T', '_', 'U', 'S', 'E', 'R', 0};
static DWORD HKCU_WSTR_SZ = sizeof(HKCU_WSTR);
static const WCHAR HKU_WSTR[] = {'H', 'K', 'E', 'Y', '_', 'U', 'S', 'E', 'R', 'S', 0};
static DWORD HKU_WSTR_SZ = sizeof(HKU_WSTR);

/*
 * Given hkey, return its string representation into buf, who is
 * assumed to hold at least (buf_sz * sizeof(*buf)) bytes
 *
 * Return 0 if successful, other value otherwise
 */
static LONG hkey_to_wstring(HKEY hkey, WCHAR *buf, DWORD buf_sz)
{
    if (hkey == HKEY_LOCAL_MACHINE)
    {
        if (buf_sz >= HKLM_WSTR_SZ)
        {
            memcpy(buf, HKLM_WSTR, HKLM_WSTR_SZ);
            return 0;
        }
        else
        {
            return -1;
        }
    }
    else if (hkey == HKEY_CURRENT_USER)
    {
        if (buf_sz >= HKCU_WSTR_SZ)
        {
            memcpy(buf, HKCU_WSTR, HKCU_WSTR_SZ);
            return 0;
        }
        else
        {
            return -1;
        }
    }
    else if (hkey == HKEY_USERS)
    {
        if (buf_sz >= HKU_WSTR_SZ)
        {
            memcpy(buf, HKU_WSTR, HKU_WSTR_SZ);
            return 0;
        }
        else
        {
            return -1;
        }
    }
    else if (hkey == HKEY_CLASSES_ROOT)
    {
        if (buf_sz >= HKCR_WSTR_SZ)
        {
            memcpy(buf, HKCR_WSTR, HKCR_WSTR_SZ);
            return 0;
        }
        else
        {
            return -1;
        }
    }

    return -2;
}

typedef struct {
    WCHAR *s;
    DWORD sz;
    DWORD base_sz;
} DisplayString;

/*
 * remain may be NULL (in which case it is assumed to be the empty
 * string
 */
static int create_base_string(DisplayString *display,
        HKEY root, WCHAR *remain,
        DWORD max_value_size)
{
    WCHAR *ret, *pret;
    WCHAR root_str[255];
    LONG st;
    DWORD ret_sz;

    st = hkey_to_wstring(root, root_str, sizeof(root_str));
    if (st) {
        return -1;
    }

    display->base_sz = strlenW(root_str) + 1;
    if (remain != NULL) {
        display->base_sz += strlenW(remain) + 1;
    }

    ret_sz = display->base_sz + max_value_size + 1;
    ret = malloc(ret_sz * sizeof(*ret));
    if (ret == NULL) {
        return -1;
    }
    display->s = ret;
    display->sz = ret_sz;

    pret = ret;
    memcpy(ret, root_str,
           sizeof(*ret) * strlenW(root_str));
    pret += strlenW(root_str);

    pret[0] = '\\';
    if (remain != NULL) {
        pret += 1;
        memcpy(pret, remain, strlenW(remain) * sizeof(*remain));
        pret += strlenW(remain);

        pret[0] = '\\';
    }

    return 0;
}

static int resize_display(DisplayString *display, WCHAR* skey)
{
    DWORD skey_sz = strlenW(skey);
    WCHAR *pdisplay;

    if(display->sz < (display->base_sz + skey_sz)) {
        display->sz = display->base_sz + skey_sz;
        display->s = realloc(display->s, sizeof(*display->s) * display->sz);
        if(display->s == NULL) {
            return -1;
        }
    }
    pdisplay = display->s + display->base_sz;
    memcpy(pdisplay, skey, skey_sz * sizeof(*skey));
    pdisplay[skey_sz] = '\0';

    return 0;
}

const static WCHAR reg_type_str[][20] = {
    {NULL},
    {'R', 'E', 'G', '_', 'S', 'Z', 0}
};

static int print_key_value(const HKEY hkey, const WCHAR* skey)
{
    static const WCHAR tabbed_ff[] = {' ', ' ', ' ', ' ', '%', 's',
                                      ' ', ' ', ' ', ' ', '%', 's',
                                      ' ', ' ', ' ', ' ', '%', 's',
                                      '\n', 0};
    DWORD type;
    BYTE data[256];
    DWORD data_sz = 256, wdata_sz;
    WCHAR *data_as_wstr;
    DWORD st;

    wdata_sz = data_sz;
    st = RegQueryValueExW(hkey, skey, NULL, &type, data, &wdata_sz);
    if (st != ERROR_SUCCESS) {
        return st;
    }

    data[wdata_sz] = '\0';
    data_as_wstr = (WCHAR*)data;
    if (type < sizeof(reg_type_str)) {
        reg_printfW(tabbed_ff, skey, reg_type_str[type], data_as_wstr);
        return 0;
    } else {
        return 1;
    }
}

static int print_skeys(HKEY root, WCHAR *p, const HKEY hkey)
{
    DWORD st;
    int i = 0;
    DWORD nskeys, nvalues;
    DWORD max_skey_sz, max_value_sz, max_value_data_sz, buf_sz;
    WCHAR *skey;
    DWORD skey_sz;
    DisplayString display;

    static WCHAR empty_line[] = {'\n', 0};
    static const WCHAR ff[] = {'%', 's', '\n', 0};

    st = RegQueryInfoKeyW(
        hkey,                    // key handle
        NULL,                    // buffer for class name
        NULL,                    // size of class string
        NULL,                    // reserved
        &nskeys,                 // number of subkeys
        &max_skey_sz,            // longest subkey size
        NULL,                    // longest class string
        &nvalues,                // number of values for this key
        &max_value_sz,           // longest value name
        &max_value_data_sz,      // longest value data
        NULL,                    // security descriptor
        NULL);                   // last write time
 
    if (st != ERROR_SUCCESS) {
        return st;
    }

    buf_sz = (max_value_sz > max_skey_sz) ? max_value_sz :
                                            max_skey_sz;
    buf_sz += 1;
    skey = malloc(sizeof(*skey) * buf_sz);
    if (skey==NULL) {
        return 1;
    }
    reg_printfW(empty_line);

    if(create_base_string(&display, root, p, buf_sz)) {
        goto clean_skey;
    }

    display.s[display.base_sz-1] = '\0';
    if (nvalues > 0) {
        reg_printfW(ff, display.s);
    }
    display.s[display.base_sz-1] = '\\';

    for (i = 0; i < nvalues; ++i) {
        skey_sz = buf_sz;
        skey[0] = '\0';
        st = RegEnumValueW(hkey, i, skey, &skey_sz, NULL,
                NULL, NULL, NULL);
        if (st != ERROR_SUCCESS) {
            goto clean_display;
        }

        st = print_key_value(hkey, skey);
        if (st) {
            goto clean_display;
        }
    }
    if (nvalues > 0) {
        reg_printfW(empty_line);
    }

    for (i = 0; i < nskeys; ++i) {
        skey_sz = buf_sz;
        st = RegEnumKeyExW(hkey, i, skey, &skey_sz, NULL,
                NULL, NULL, NULL);
        if (st != ERROR_SUCCESS) {
            goto clean_display;
        }
        memcpy(display.s + display.base_sz, skey, skey_sz * sizeof(*skey));
        display.s[display.base_sz + skey_sz] = '\0';
        reg_printfW(ff, display.s);
    }
    free(display.s);
    free(skey);

    return 0;

clean_display:
    free(display.s);
clean_skey:
    free(skey);

    return 1;
}

static int query_single_value(HKEY root, WCHAR* p, HKEY hkey,
        const WCHAR* value_name)
{
    static WCHAR ff[] = {'%', 's', '\n', 0};
    DisplayString display;
    static WCHAR empty_line[] = {'\n', 0};
    DWORD st;

    reg_printfW(empty_line);

    st = RegQueryValueExW(hkey, value_name, NULL, NULL, NULL, NULL);
    if (st != ERROR_SUCCESS) {
        /* printed here for compatibility with windows reg.exe */
        reg_printfW(empty_line);
        return 1;
    }

    st = create_base_string(&display, root, p, strlenW(value_name));
    if(st) {
        return 1;
    }

    display.s[display.base_sz-1] = '\0';
    reg_printfW(ff, display.s);
    display.s[display.base_sz-1] = '\\';

    st = print_key_value(hkey, value_name);
    if (st) {
        goto clean_display;
    }
    reg_printfW(empty_line);
    free(display.s);
    return 0;

clean_display:
    free(display.s);
    return 1;
}

static int reg_query(WCHAR *key_name, WCHAR *value_name, BOOL value_empty,
    BOOL subkey)
{
    HKEY root;
    HKEY hkey;
    LONG st;
    WCHAR *p;

    root = get_rootkey(key_name);
    if (root == NULL) {
        reg_message(STRING_INVALID_KEY);
        return 1;
    }

    p = strchrW(key_name, '\\');
    if (p != NULL) {
        p += 1;
    }

    st = RegOpenKeyExW(root, p, 0, KEY_READ, &hkey);
    if (st == ERROR_SUCCESS) {
        if (value_name == NULL) {
            st = print_skeys(root, p, hkey);
            if (st) {
                goto close_key;
            }
        } else {
            st = query_single_value(root, p, hkey, value_name);
        }
    } else {
        return 1;
    }
    RegCloseKey(hkey);

    return 0;

close_key:
    RegCloseKey(hkey);
    return 1;
}

int wmain(int argc, WCHAR *argvW[])
{
    int i;

    static const WCHAR addW[] = {'a','d','d',0};
    static const WCHAR deleteW[] = {'d','e','l','e','t','e',0};
    static const WCHAR queryW[] = {'q','u','e','r','y',0};
    static const WCHAR slashDW[] = {'/','d',0};
    static const WCHAR slashFW[] = {'/','f',0};
    static const WCHAR slashHW[] = {'/','h',0};
    static const WCHAR slashSW[] = {'/','s',0};
    static const WCHAR slashTW[] = {'/','t',0};
    static const WCHAR slashVW[] = {'/','v',0};
    static const WCHAR slashVAW[] = {'/','v','a',0};
    static const WCHAR slashVEW[] = {'/','v','e',0};
    static const WCHAR slashHelpW[] = {'/','?',0};

    if (argc < 2 || !lstrcmpW(argvW[1], slashHelpW)
                 || !lstrcmpiW(argvW[1], slashHW))
    {
        reg_message(STRING_USAGE);
        return 0;
    }

    if (!lstrcmpiW(argvW[1], addW))
    {
        WCHAR *key_name, *value_name = NULL, *type = NULL, separator = '\0', *data = NULL;
        BOOL value_empty = FALSE, force = FALSE;

        if (argc < 3)
        {
            reg_message(STRING_INVALID_CMDLINE);
            return 1;
        }
        else if (argc == 3 && (!lstrcmpW(argvW[2], slashHelpW) ||
                               !lstrcmpiW(argvW[2], slashHW)))
        {
            reg_message(STRING_ADD_USAGE);
            return 0;
        }

        key_name = argvW[2];
        for (i = 1; i < argc; i++)
        {
            if (!lstrcmpiW(argvW[i], slashVW))
                value_name = argvW[++i];
            else if (!lstrcmpiW(argvW[i], slashVEW))
                value_empty = TRUE;
            else if (!lstrcmpiW(argvW[i], slashTW))
                type = argvW[++i];
            else if (!lstrcmpiW(argvW[i], slashSW))
                separator = argvW[++i][0];
            else if (!lstrcmpiW(argvW[i], slashDW))
                data = argvW[++i];
            else if (!lstrcmpiW(argvW[i], slashFW))
                force = TRUE;
        }
        return reg_add(key_name, value_name, value_empty, type, separator,
            data, force);
    }
    else if (!lstrcmpiW(argvW[1], deleteW))
    {
        WCHAR *key_name, *value_name = NULL;
        BOOL value_empty = FALSE, value_all = FALSE, force = FALSE;

        if (argc < 3)
        {
            reg_message(STRING_INVALID_CMDLINE);
            return 1;
        }
        else if (argc == 3 && (!lstrcmpW(argvW[2], slashHelpW) ||
                               !lstrcmpiW(argvW[2], slashHW)))
        {
            reg_message(STRING_DELETE_USAGE);
            return 0;
        }

        key_name = argvW[2];
        for (i = 1; i < argc; i++)
        {
            if (!lstrcmpiW(argvW[i], slashVW))
                value_name = argvW[++i];
            else if (!lstrcmpiW(argvW[i], slashVEW))
                value_empty = TRUE;
            else if (!lstrcmpiW(argvW[i], slashVAW))
                value_all = TRUE;
            else if (!lstrcmpiW(argvW[i], slashFW))
                force = TRUE;
        }
        return reg_delete(key_name, value_name, value_empty, value_all, force);
    }
    else if (!lstrcmpiW(argvW[1], queryW))
    {
        WCHAR *key_name, *value_name = NULL;
        BOOL value_empty = FALSE, subkey = FALSE;

        if (argc < 3)
        {
            reg_message(STRING_INVALID_CMDLINE);
            return 1;
        }
        else if (argc == 3 && (!lstrcmpW(argvW[2], slashHelpW) ||
                               !lstrcmpiW(argvW[2], slashHW)))
        {
            reg_message(STRING_QUERY_USAGE);
            return 0;
        }

        key_name = argvW[2];
        for (i = 1; i < argc; i++)
        {
            if (!lstrcmpiW(argvW[i], slashVW))
                value_name = argvW[++i];
            else if (!lstrcmpiW(argvW[i], slashVEW))
                value_empty = TRUE;
            else if (!lstrcmpiW(argvW[i], slashSW))
                subkey = TRUE;
        }
        return reg_query(key_name, value_name, value_empty, subkey);
    }
    else
    {
        reg_message(STRING_INVALID_CMDLINE);
        return 1;
    }
}
