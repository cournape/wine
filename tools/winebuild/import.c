/*
 * DLL imports support
 *
 * Copyright 2000 Alexandre Julliard
 *           2000 Eric Pouech
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "config.h"
#include "wine/port.h"

#include <fcntl.h>
#include <stdio.h>
#include <string.h>

#include "build.h"

struct import
{
    char        *dll;         /* dll name */
    int          delay;       /* delay or not dll loading ? */
    char       **exports;     /* functions exported from this dll */
    int          nb_exports;  /* number of exported functions */
    char       **imports;     /* functions we want to import from this dll */
    int          nb_imports;  /* number of imported functions */
    int          lineno;      /* line in .spec file where import is defined */
};

static char **undef_symbols;  /* list of undefined symbols */
static int nb_undef_symbols = -1;
static int undef_size;

static char **ignore_symbols; /* list of symbols to ignore */
static int nb_ignore_symbols;
static int ignore_size;

static struct import **dll_imports = NULL;
static int nb_imports = 0;      /* number of imported dlls (delayed or not) */
static int nb_delayed = 0;      /* number of delayed dlls */
static int total_imports = 0;   /* total number of imported functions */
static int total_delayed = 0;   /* total number of imported functions in delayed DLLs */

/* compare function names; helper for resolve_imports */
static int name_cmp( const void *name, const void *entry )
{
    return strcmp( *(char **)name, *(char **)entry );
}

/* locate a symbol in a (sorted) list */
inline static const char *find_symbol( const char *name, char **table, int size )
{
    char **res = NULL;

    if (table) {
        res = bsearch( &name, table, size, sizeof(*table), name_cmp );
    }

    return res ? *res : NULL;
}

/* sort a symbol table */
inline static void sort_symbols( char **table, int size )
{
    if (table )
        qsort( table, size, sizeof(*table), name_cmp );
}

/* open the .so library for a given dll in a specified path */
static char *try_library_path( const char *path, const char *name )
{
    char *buffer;
    int fd;

    buffer = xmalloc( strlen(path) + strlen(name) + 9 );
    sprintf( buffer, "%s/%s", path, name );
    strcat( buffer, ".so" );
    /* check if the file exists */
    if ((fd = open( buffer, O_RDONLY )) != -1)
    {
        close( fd );
        return buffer;
    }
    free( buffer );
    return NULL;
}

/* open the .so library for a given dll */
static char *open_library( const char *name )
{
    char *fullname;
    int i;

    for (i = 0; i < nb_lib_paths; i++)
    {
        if ((fullname = try_library_path( lib_path[i], name ))) return fullname;
    }
    if (!(fullname = try_library_path( ".", name )))
        fatal_error( "could not open .so file for %s\n", name );
    return fullname;
}

/* read in the list of exported symbols of a .so */
static void read_exported_symbols( const char *name, struct import *imp )
{
    FILE *f;
    char buffer[1024];
    char *fullname, *cmdline;
    const char *ext;
    int size, err;

    imp->exports    = NULL;
    imp->nb_exports = size = 0;

    if (!(ext = strrchr( name, '.' ))) ext = name + strlen(name);

    if (!(fullname = open_library( name ))) return;
    cmdline = xmalloc( strlen(fullname) + 7 );
    sprintf( cmdline, "nm -D %s", fullname );
    free( fullname );

    if (!(f = popen( cmdline, "r" )))
        fatal_error( "Cannot execute '%s'\n", cmdline );

    while (fgets( buffer, sizeof(buffer), f ))
    {
        char *p = buffer + strlen(buffer) - 1;
        if (p < buffer) continue;
        if (*p == '\n') *p-- = 0;
        if (!(p = strstr( buffer, "__wine_dllexport_" ))) continue;
        p += 17;
        if (strncmp( p, name, ext - name )) continue;
        p += ext - name;
        if (*p++ != '_') continue;

        if (imp->nb_exports == size)
        {
            size += 128;
            imp->exports = xrealloc( imp->exports, size * sizeof(*imp->exports) );
        }
        imp->exports[imp->nb_exports++] = xstrdup( p );
    }
    if ((err = pclose( f ))) fatal_error( "%s error %d\n", cmdline, err );
    free( cmdline );
    sort_symbols( imp->exports, imp->nb_exports );
}

/* add a dll to the list of imports */
void add_import_dll( const char *name, int delay )
{
    struct import *imp;
    char *fullname;
    int i;

    fullname = xmalloc( strlen(name) + 5 );
    strcpy( fullname, name );
    if (!strchr( fullname, '.' )) strcat( fullname, ".dll" );

    /* check if we already imported it */
    for (i = 0; i < nb_imports; i++)
    {
        if (!strcmp( dll_imports[i]->dll, fullname ))
        {
            free( fullname );
            return;
        }
    }

    imp = xmalloc( sizeof(*imp) );
    imp->dll        = fullname;
    imp->delay      = delay;
    imp->imports    = NULL;
    imp->nb_imports = 0;
    /* GetToken for the file name has swallowed the '\n', hence pointing to next line */
    imp->lineno = current_line - 1;

    if (delay) nb_delayed++;
    read_exported_symbols( fullname, imp );

    dll_imports = xrealloc( dll_imports, (nb_imports+1) * sizeof(*dll_imports) );
    dll_imports[nb_imports++] = imp;
}

/* Add a symbol to the ignored symbol list */
void add_ignore_symbol( const char *name )
{
    if (nb_ignore_symbols == ignore_size)
    {
        ignore_size += 32;
        ignore_symbols = xrealloc( ignore_symbols, ignore_size * sizeof(*ignore_symbols) );
    }
    ignore_symbols[nb_ignore_symbols++] = xstrdup( name );
}

/* add a function to the list of imports from a given dll */
static void add_import_func( struct import *imp, const char *name )
{
    imp->imports = xrealloc( imp->imports, (imp->nb_imports+1) * sizeof(*imp->imports) );
    imp->imports[imp->nb_imports++] = xstrdup( name );
    total_imports++;
    if (imp->delay) total_delayed++;
}

/* add a symbol to the undef list */
inline static void add_undef_symbol( const char *name )
{
    if (nb_undef_symbols == undef_size)
    {
        undef_size += 128;
        undef_symbols = xrealloc( undef_symbols, undef_size * sizeof(*undef_symbols) );
    }
    undef_symbols[nb_undef_symbols++] = xstrdup( name );
}

/* remove all the holes in the undefined symbol list; return the number of removed symbols */
static int remove_symbol_holes(void)
{
    int i, off;
    for (i = off = 0; i < nb_undef_symbols; i++)
    {
        if (!undef_symbols[i]) off++;
        else undef_symbols[i - off] = undef_symbols[i];
    }
    nb_undef_symbols -= off;
    return off;
}

/* add the extra undefined symbols that will be contained in the generated spec file itself */
static void add_extra_undef_symbols(void)
{
    const char *extras[10];
    int i, count = 0;

#define ADD_SYM(name) \
    do { if (!find_symbol( extras[count] = (name), undef_symbols, \
                           nb_undef_symbols )) count++; } while(0)

    sort_symbols( undef_symbols, nb_undef_symbols );

    /* add symbols that will be contained in the spec file itself */
    switch (SpecMode)
    {
    case SPEC_MODE_DLL:
        break;
    case SPEC_MODE_GUIEXE:
        ADD_SYM( "GetCommandLineA" );
        ADD_SYM( "GetStartupInfoA" );
        ADD_SYM( "GetModuleHandleA" );
        /* fall through */
    case SPEC_MODE_CUIEXE:
        ADD_SYM( "__wine_get_main_args" );
        ADD_SYM( "ExitProcess" );
        break;
    case SPEC_MODE_GUIEXE_UNICODE:
        ADD_SYM( "GetCommandLineA" );
        ADD_SYM( "GetStartupInfoA" );
        ADD_SYM( "GetModuleHandleA" );
        /* fall through */
    case SPEC_MODE_CUIEXE_UNICODE:
        ADD_SYM( "__wine_get_wmain_args" );
        ADD_SYM( "ExitProcess" );
        break;
    }
    ADD_SYM( "RtlRaiseException" );
    if (nb_delayed)
    {
       ADD_SYM( "LoadLibraryA" );
       ADD_SYM( "GetProcAddress" );
    }

    for (i = 0; i < nb_entry_points; i++)
    {
        ORDDEF *odp = EntryPoints[i];
        if (odp->flags & FLAG_REGISTER)
        {
            ADD_SYM( "__wine_call_from_32_regs" );
            break;
        }
    }

    if (count)
    {
        for (i = 0; i < count; i++) add_undef_symbol( extras[i] );
        sort_symbols( undef_symbols, nb_undef_symbols );
    }
}

/* warn if a given dll is not used, but check forwards first */
static void warn_unused( const struct import* imp )
{
    int i, curline;
    size_t len = strlen(imp->dll);
    const char *p = strchr( imp->dll, '.' );
    if (p && !strcasecmp( p, ".dll" )) len = p - imp->dll;

    for (i = Base; i <= Limit; i++)
    {
        ORDDEF *odp = Ordinals[i];
        if (!odp || odp->type != TYPE_FORWARD) continue;
        if (!strncasecmp( odp->link_name, imp->dll, len ) &&
            odp->link_name[len] == '.')
            return;  /* found an import, do not warn */
    }
    /* switch current_line temporarily to the line of the import declaration */
    curline = current_line;
    current_line = imp->lineno;
    warning( "%s imported but no symbols used\n", imp->dll );
    current_line = curline;
}

/* read in the list of undefined symbols */
void read_undef_symbols( const char *name )
{
    FILE *f;
    char buffer[1024];
    int err;

    undef_size = nb_undef_symbols = 0;

    sprintf( buffer, "nm -u %s", name );
    if (!(f = popen( buffer, "r" )))
        fatal_error( "Cannot execute '%s'\n", buffer );

    while (fgets( buffer, sizeof(buffer), f ))
    {
        char *p = buffer + strlen(buffer) - 1;
        if (p < buffer) continue;
        if (*p == '\n') *p-- = 0;
        p = buffer; while (*p == ' ') p++;
        add_undef_symbol( p );
    }
    if ((err = pclose( f ))) fatal_error( "nm -u %s error %d\n", name, err );
}

static void remove_ignored_symbols(void)
{
    int i;

    sort_symbols( ignore_symbols, nb_ignore_symbols );
    for (i = 0; i < nb_undef_symbols; i++)
    {
        if (find_symbol( undef_symbols[i], ignore_symbols, nb_ignore_symbols ))
        {
            free( undef_symbols[i] );
            undef_symbols[i] = NULL;
        }
    }
    remove_symbol_holes();
}

/* resolve the imports for a Win32 module */
int resolve_imports( void )
{
    int i, j;

    if (nb_undef_symbols == -1) return 0; /* no symbol file specified */

    add_extra_undef_symbols();
    remove_ignored_symbols();

    for (i = 0; i < nb_imports; i++)
    {
        struct import *imp = dll_imports[i];

        for (j = 0; j < nb_undef_symbols; j++)
        {
            const char *res = find_symbol( undef_symbols[j], imp->exports, imp->nb_exports );
            if (res)
            {
                add_import_func( imp, res );
                free( undef_symbols[j] );
                undef_symbols[j] = NULL;
            }
        }
        /* remove all the holes in the undef symbols list */
        if (!remove_symbol_holes()) warn_unused( imp );
    }
    return 1;
}

/* output the import table of a Win32 module */
static int output_immediate_imports( FILE *outfile )
{
    int i, j, pos;
    int nb_imm = nb_imports - nb_delayed;

    if (!nb_imm) goto done;

    /* main import header */

    fprintf( outfile, "\nstatic struct {\n" );
    fprintf( outfile, "  struct {\n" );
    fprintf( outfile, "    void        *OriginalFirstThunk;\n" );
    fprintf( outfile, "    unsigned int TimeDateStamp;\n" );
    fprintf( outfile, "    unsigned int ForwarderChain;\n" );
    fprintf( outfile, "    const char  *Name;\n" );
    fprintf( outfile, "    void        *FirstThunk;\n" );
    fprintf( outfile, "  } imp[%d];\n", nb_imm+1 );
    fprintf( outfile, "  const char *data[%d];\n",
             total_imports - total_delayed + nb_imm );
    fprintf( outfile, "} imports = {\n  {\n" );

    /* list of dlls */

    for (i = j = 0; i < nb_imports; i++)
    {
        if (dll_imports[i]->delay) continue;
        fprintf( outfile, "    { 0, 0, 0, \"%s\", &imports.data[%d] },\n",
                 dll_imports[i]->dll, j );
        j += dll_imports[i]->nb_imports + 1;
    }

    fprintf( outfile, "    { 0, 0, 0, 0, 0 },\n" );
    fprintf( outfile, "  },\n  {\n" );

    /* list of imported functions */

    for (i = 0; i < nb_imports; i++)
    {
        if (dll_imports[i]->delay) continue;
        fprintf( outfile, "    /* %s */\n", dll_imports[i]->dll );
        for (j = 0; j < dll_imports[i]->nb_imports; j++)
            fprintf( outfile, "    \"\\0\\0%s\",\n", dll_imports[i]->imports[j] );
        fprintf( outfile, "    0,\n" );
    }
    fprintf( outfile, "  }\n};\n\n" );

    /* thunks for imported functions */

    fprintf( outfile, "#ifndef __GNUC__\nstatic void __asm__dummy_import(void) {\n#endif\n\n" );
    pos = 20 * (nb_imm + 1);  /* offset of imports.data from start of imports */
    fprintf( outfile, "asm(\".data\\n\\t.align %d\\n\"\n", get_alignment(8) );
    for (i = 0; i < nb_imports; i++)
    {
        if (dll_imports[i]->delay) continue;
        for (j = 0; j < dll_imports[i]->nb_imports; j++, pos += 4)
        {
            fprintf( outfile, "    \"\\t" __ASM_FUNC("%s") "\\n\"\n",
                     dll_imports[i]->imports[j] );
            fprintf( outfile, "    \"\\t.globl " PREFIX "%s\\n\"\n",
                     dll_imports[i]->imports[j] );




            fprintf( outfile, "    \"" PREFIX "%s:\\n\\t", dll_imports[i]->imports[j] );

#if defined(__i386__)
            if (strstr( dll_imports[i]->imports[j], "__wine_call_from_16" ))
                fprintf( outfile, ".byte 0x2e\\n\\tjmp *(imports+%d)\\n\\tnop\\n", pos );
            else
                fprintf( outfile, "jmp *(imports+%d)\\n\\tmovl %%esi,%%esi\\n", pos );
#elif defined(__sparc__)
            if ( !UsePIC )
            {
                fprintf( outfile, "sethi %%hi(imports+%d), %%g1\\n\\t", pos );
                fprintf( outfile, "ld [%%g1+%%lo(imports+%d)], %%g1\\n\\t", pos );
                fprintf( outfile, "jmp %%g1\\n\\tnop\\n" );
            }
            else
            {
                /* Hmpf.  Stupid sparc assembler always interprets global variable
                   names as GOT offsets, so we have to do it the long way ... */
                fprintf( outfile, "save %%sp, -96, %%sp\\n" );
                fprintf( outfile, "0:\\tcall 1f\\n\\tnop\\n" );
                fprintf( outfile, "1:\\tsethi %%hi(imports+%d-0b), %%g1\\n\\t", pos );
                fprintf( outfile, "or %%g1, %%lo(imports+%d-0b), %%g1\\n\\t", pos );
                fprintf( outfile, "ld [%%g1+%%o7], %%g1\\n\\t" );
                fprintf( outfile, "jmp %%g1\\n\\trestore\\n" );
            }

#elif defined(__PPC__)
            fprintf(outfile, "\taddi 1, 1, -0x4\\n\"\n");
            fprintf(outfile, "\t\"\\tstw 9, 0(1)\\n\"\n");
            fprintf(outfile, "\t\"\\taddi 1, 1, -0x4\\n\"\n");
            fprintf(outfile, "\t\"\\tstw 8, 0(1)\\n\"\n");
            fprintf(outfile, "\t\"\\taddi 1, 1, -0x4\\n\"\n");
            fprintf(outfile, "\t\"\\tstw 7, 0(1)\\n\"\n");

            fprintf(outfile, "\t\"\\tlis 9,imports+%d@ha\\n\"\n", pos);
            fprintf(outfile, "\t\"\\tla 8,imports+%d@l(9)\\n\"\n", pos);
            fprintf(outfile, "\t\"\\tlwz 7, 0(8)\\n\"\n");
            fprintf(outfile, "\t\"\\tmtctr 7\\n\"\n");

            fprintf(outfile, "\t\"\\tlwz 7, 0(1)\\n\"\n");
            fprintf(outfile, "\t\"\\taddi 1, 1, 0x4\\n\"\n");
            fprintf(outfile, "\t\"\\tlwz 8, 0(1)\\n\"\n");
            fprintf(outfile, "\t\"\\taddi 1, 1, 0x4\\n\"\n");
            fprintf(outfile, "\t\"\\tlwz 9, 0(1)\\n\"\n");
            fprintf(outfile, "\t\"\\taddi 1, 1, 0x4\\n\"\n");
            fprintf(outfile, "\t\"\\tbctr\\n");
#else
#error You need to define import thunks for your architecture!
#endif
            fprintf( outfile, "\"\n" );
        }
        pos += 4;
    }
    fprintf( outfile, "\".previous\");\n#ifndef __GNUC__\n}\n#endif\n\n" );

 done:
    return nb_imm;
}

/* output the delayed import table of a Win32 module */
static int output_delayed_imports( FILE *outfile )
{
    int i, idx, j, pos;

    if (!nb_delayed) goto done;

    for (i = 0; i < nb_imports; i++)
    {
        if (!dll_imports[i]->delay) continue;
        fprintf( outfile, "static void *__wine_delay_imp_%d_hmod;\n", i);
        for (j = 0; j < dll_imports[i]->nb_imports; j++)
        {
            fprintf( outfile, "void __wine_delay_imp_%d_%s();\n",
                     i, dll_imports[i]->imports[j] );
        }
    }
    fprintf( outfile, "\n" );
    fprintf( outfile, "static struct {\n" );
    fprintf( outfile, "  struct ImgDelayDescr {\n" );
    fprintf( outfile, "    unsigned int  grAttrs;\n" );
    fprintf( outfile, "    const char   *szName;\n" );
    fprintf( outfile, "    void        **phmod;\n" );
    fprintf( outfile, "    void        **pIAT;\n" );
    fprintf( outfile, "    const char  **pINT;\n" );
    fprintf( outfile, "    void*         pBoundIAT;\n" );
    fprintf( outfile, "    void*         pUnloadIAT;\n" );
    fprintf( outfile, "    unsigned long dwTimeStamp;\n" );
    fprintf( outfile, "  } imp[%d];\n", nb_delayed );
    fprintf( outfile, "  void         *IAT[%d];\n", total_delayed );
    fprintf( outfile, "  const char   *INT[%d];\n", total_delayed );
    fprintf( outfile, "} delay_imports = {\n" );
    fprintf( outfile, "  {\n" );
    for (i = j = 0; i < nb_imports; i++)
    {
        if (!dll_imports[i]->delay) continue;
        fprintf( outfile, "    { 0, \"%s\", &__wine_delay_imp_%d_hmod, &delay_imports.IAT[%d], &delay_imports.INT[%d], 0, 0, 0 },\n",
                 dll_imports[i]->dll, i, j, j );
        j += dll_imports[i]->nb_imports;
    }
    fprintf( outfile, "  },\n  {\n" );
    for (i = 0; i < nb_imports; i++)
    {
        if (!dll_imports[i]->delay) continue;
        fprintf( outfile, "    /* %s */\n", dll_imports[i]->dll );
        for (j = 0; j < dll_imports[i]->nb_imports; j++)
        {
            fprintf( outfile, "    &__wine_delay_imp_%d_%s,\n", i, dll_imports[i]->imports[j] );
        }
    }
    fprintf( outfile, "  },\n  {\n" );
    for (i = 0; i < nb_imports; i++)
    {
        if (!dll_imports[i]->delay) continue;
        fprintf( outfile, "    /* %s */\n", dll_imports[i]->dll );
        for (j = 0; j < dll_imports[i]->nb_imports; j++)
        {
            fprintf( outfile, "    \"\\0\\0%s\",\n", dll_imports[i]->imports[j] );
        }
    }
    fprintf( outfile, "  }\n};\n\n" );

    /* check if there's some stub defined. if so, exception struct 
     *  is already defined, so don't emit it twice 
     */
    for (i = 0; i < nb_entry_points; i++) if (EntryPoints[i]->type == TYPE_STUB) break;

    if (i == nb_entry_points) {
       fprintf( outfile, "struct exc_record {\n" );
       fprintf( outfile, "  unsigned int code, flags;\n" );
       fprintf( outfile, "  void *rec, *addr;\n" );
       fprintf( outfile, "  unsigned int params;\n" );
       fprintf( outfile, "  const void *info[15];\n" );
       fprintf( outfile, "};\n\n" );
       fprintf( outfile, "extern void __stdcall RtlRaiseException( struct exc_record * );\n" );
    }

    fprintf( outfile, "extern void * __stdcall LoadLibraryA(const char*);\n");
    fprintf( outfile, "extern void * __stdcall GetProcAddress(void *, const char*);\n");
    fprintf( outfile, "\n" );

    fprintf( outfile, "void *__stdcall __wine_delay_load( int idx_nr )\n" );
    fprintf( outfile, "{\n" );
    fprintf( outfile, "  int idx = idx_nr >> 16, nr = idx_nr & 0xffff;\n" );
    fprintf( outfile, "  struct ImgDelayDescr *imd = delay_imports.imp + idx;\n" );
    fprintf( outfile, "  void **pIAT = imd->pIAT + nr;\n" );
    fprintf( outfile, "  const char** pINT = imd->pINT + nr;\n" );
    fprintf( outfile, "  void *fn;\n\n" );

    fprintf( outfile, "  if (!*imd->phmod) *imd->phmod = LoadLibraryA(imd->szName);\n" );
    fprintf( outfile, "  if (*imd->phmod && (fn = GetProcAddress(*imd->phmod, *pINT + 2)))\n");
    fprintf( outfile, "    /* patch IAT with final value */\n" );
    fprintf( outfile, "    return *pIAT = fn;\n" );
    fprintf( outfile, "  else {\n");
    fprintf( outfile, "    struct exc_record rec;\n" );
    fprintf( outfile, "    rec.code    = 0x80000100;\n" );
    fprintf( outfile, "    rec.flags   = 1;\n" );
    fprintf( outfile, "    rec.rec     = 0;\n" );
    fprintf( outfile, "    rec.params  = 2;\n" );
    fprintf( outfile, "    rec.info[0] = imd->szName;\n" );
    fprintf( outfile, "    rec.info[1] = *pINT + 2;\n" );
    fprintf( outfile, "#ifdef __GNUC__\n" );
    fprintf( outfile, "    rec.addr = __builtin_return_address(1);\n" );
    fprintf( outfile, "#else\n" );
    fprintf( outfile, "    rec.addr = 0;\n" );
    fprintf( outfile, "#endif\n" );
    fprintf( outfile, "    for (;;) RtlRaiseException( &rec );\n" );
    fprintf( outfile, "    return 0; /* shouldn't go here */\n" );
    fprintf( outfile, "  }\n}\n\n" );

    fprintf( outfile, "#ifndef __GNUC__\n" );
    fprintf( outfile, "static void __asm__dummy_delay_import(void) {\n" );
    fprintf( outfile, "#endif\n" );

    fprintf( outfile, "asm(\".align %d\\n\"\n", get_alignment(8) );
    fprintf( outfile, "    \"\\t" __ASM_FUNC("__wine_delay_load_asm") "\\n\"\n" );
    fprintf( outfile, "    \"" PREFIX "__wine_delay_load_asm:\\n\"\n" );
#if defined(__i386__)
    fprintf( outfile, "    \"\\tpushl %%ecx\\n\\tpushl %%edx\\n\\tpushl %%eax\\n\"\n" );
    fprintf( outfile, "    \"\\tcall __wine_delay_load\\n\"\n" );
    fprintf( outfile, "    \"\\tpopl %%edx\\n\\tpopl %%ecx\\n\\tjmp *%%eax\\n\"\n" );
#elif defined(__sparc__)
    fprintf( outfile, "    \"\\tsave %%sp, -96, %%sp\\n\"\n" );
    fprintf( outfile, "    \"\\tcall __wine_delay_load\\n\"\n" );
    fprintf( outfile, "    \"\\tmov %%g1, %%o0\\n\"\n" );
    fprintf( outfile, "    \"\\tjmp %%o0\\n\\trestore\\n\"\n" );
#elif defined(__PPC__)
    fprintf(outfile, "#error: DELAYED IMPORTS NOT SUPPORTED ON PPC!!!\n");
#else
#error You need to defined delayed import thunks for your architecture!
#endif

    for (i = idx = 0; i < nb_imports; i++)
    {
        if (!dll_imports[i]->delay) continue;
        for (j = 0; j < dll_imports[i]->nb_imports; j++)
        {
            char buffer[128];
            sprintf( buffer, "__wine_delay_imp_%d_%s", i, dll_imports[i]->imports[j]);
            fprintf( outfile, "    \"\\t" __ASM_FUNC("%s") "\\n\"\n", buffer );
            fprintf( outfile, "    \"" PREFIX "%s:\\n\"\n", buffer );
#if defined(__i386__)
            fprintf( outfile, "    \"\\tmovl $%d, %%eax\\n\"\n", (idx << 16) | j );
            fprintf( outfile, "    \"\\tjmp __wine_delay_load_asm\\n\"\n" );
#elif defined(__sparc__)
            fprintf( outfile, "    \"\\tset %d, %%g1\\n\"\n", (idx << 16) | j );
            fprintf( outfile, "    \"\\tb,a __wine_delay_load_asm\\n\"\n" );
#elif defined(__PPC__)
            fprintf(outfile, "#error: DELAYED IMPORTS NOT SUPPORTED ON PPC!!!\n");
#else
#error You need to defined delayed import thunks for your architecture!
#endif
        }
        idx++;
    }

    fprintf( outfile, "\n    \".data\\n\\t.align %d\\n\"\n", get_alignment(8) );
    pos = nb_delayed * 32;
    for (i = 0; i < nb_imports; i++)
    {
        if (!dll_imports[i]->delay) continue;
        for (j = 0; j < dll_imports[i]->nb_imports; j++, pos += 4)
        {
            fprintf( outfile, "    \"\\t" __ASM_FUNC("%s") "\\n\"\n",
                     dll_imports[i]->imports[j] );
            fprintf( outfile, "    \"\\t.globl " PREFIX "%s\\n\"\n",
                     dll_imports[i]->imports[j] );
            fprintf( outfile, "    \"" PREFIX "%s:\\n\\t", dll_imports[i]->imports[j] );
#if defined(__i386__)
            if (strstr( dll_imports[i]->imports[j], "__wine_call_from_16" ))
                fprintf( outfile, ".byte 0x2e\\n\\tjmp *(delay_imports+%d)\\n\\tnop\\n", pos );
            else
                fprintf( outfile, "jmp *(delay_imports+%d)\\n\\tmovl %%esi,%%esi\\n", pos );
#elif defined(__sparc__)
            if ( !UsePIC )
            {
                fprintf( outfile, "sethi %%hi(delay_imports+%d), %%g1\\n\\t", pos );
                fprintf( outfile, "ld [%%g1+%%lo(delay_imports+%d)], %%g1\\n\\t", pos );
                fprintf( outfile, "jmp %%g1\\n\\tnop\\n" );
            }
            else
            {
                /* Hmpf.  Stupid sparc assembler always interprets global variable
                   names as GOT offsets, so we have to do it the long way ... */
                fprintf( outfile, "save %%sp, -96, %%sp\\n" );
                fprintf( outfile, "0:\\tcall 1f\\n\\tnop\\n" );
                fprintf( outfile, "1:\\tsethi %%hi(delay_imports+%d-0b), %%g1\\n\\t", pos );
                fprintf( outfile, "or %%g1, %%lo(delay_imports+%d-0b), %%g1\\n\\t", pos );
                fprintf( outfile, "ld [%%g1+%%o7], %%g1\\n\\t" );
                fprintf( outfile, "jmp %%g1\\n\\trestore\\n" );
            }

#elif defined(__PPC__)
            fprintf(outfile, "#error: DELAYED IMPORTS NOT SUPPORTED ON PPC!!!\n");
#else
#error You need to define delayed import thunks for your architecture!
#endif
            fprintf( outfile, "\"\n" );
        }
    }
    fprintf( outfile, "\".previous\");\n" );
    fprintf( outfile, "#ifndef __GNUC__\n" );
    fprintf( outfile, "}\n" );
    fprintf( outfile, "#endif\n" );
    fprintf( outfile, "\n" );

 done:
    return nb_delayed;
}

/* output the import and delayed import tables of a Win32 module
 * returns number of DLLs exported in 'immediate' mode
 */
int output_imports( FILE *outfile )
{
   output_delayed_imports( outfile );
   return output_immediate_imports( outfile );
}
