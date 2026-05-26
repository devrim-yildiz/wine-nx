#include <switch.h>

#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "horizon_mman.h"

u32 __nx_applet_type = AppletType_Application;
size_t __nx_heap_size = 256 * 1024 * 1024;
unsigned char __attribute__((aligned(16))) __nx_exception_stack[0x10000];
uint64_t __nx_exception_stack_size = sizeof(__nx_exception_stack);

#define PE_FILE_SIZE 0x400
#define PE_HEADER_SIZE 0x200
#define PE_TEXT_RVA 0x1000
#define PE_TEXT_RAW 0x200
#define PE_TEXT_RAW_SIZE 0x200
#define PE_IMAGE_SIZE 0x2000
#define PE_IMAGE_BASE 0x140000000ULL
#define PE_ENTRY_RESULT 0x574e5850U

static FILE *log_file;
static int failures;

static void log_line( const char *fmt, ... )
{
    va_list args;

    va_start( args, fmt );
    vprintf( fmt, args );
    va_end( args );
    putchar( '\n' );

    if (log_file)
    {
        va_start( args, fmt );
        vfprintf( log_file, fmt, args );
        va_end( args );
        fputc( '\n', log_file );
        fflush( log_file );
    }
    consoleUpdate( NULL );
}

static int check_bool( const char *name, int ok )
{
    if (ok)
    {
        log_line( "[OK] %s", name );
        return 1;
    }

    log_line( "[FAIL] %s", name );
    failures++;
    return 0;
}

static void put16( unsigned char *ptr, uint16_t value )
{
    ptr[0] = value & 0xff;
    ptr[1] = value >> 8;
}

static void put32( unsigned char *ptr, uint32_t value )
{
    ptr[0] = value & 0xff;
    ptr[1] = (value >> 8) & 0xff;
    ptr[2] = (value >> 16) & 0xff;
    ptr[3] = value >> 24;
}

static void put64( unsigned char *ptr, uint64_t value )
{
    put32( ptr, value & 0xffffffffU );
    put32( ptr + 4, value >> 32 );
}

static uint16_t get16( const unsigned char *ptr )
{
    return ptr[0] | ((uint16_t)ptr[1] << 8);
}

static uint32_t get32( const unsigned char *ptr )
{
    return ptr[0] | ((uint32_t)ptr[1] << 8) | ((uint32_t)ptr[2] << 16) | ((uint32_t)ptr[3] << 24);
}

static uint64_t get64( const unsigned char *ptr )
{
    return get32( ptr ) | ((uint64_t)get32( ptr + 4 ) << 32);
}

static uint32_t arm64_movz_w( unsigned int reg, uint16_t imm, unsigned int shift )
{
    return 0x52800000U | (((shift / 16) & 3) << 21) | ((uint32_t)imm << 5) | (reg & 31);
}

static uint32_t arm64_movk_w( unsigned int reg, uint16_t imm, unsigned int shift )
{
    return 0x72800000U | (((shift / 16) & 3) << 21) | ((uint32_t)imm << 5) | (reg & 31);
}

static void build_importless_arm64_pe( unsigned char pe[PE_FILE_SIZE] )
{
    const size_t pe_offset = 0x80;
    const size_t coff = pe_offset + 4;
    const size_t opt = coff + 20;
    const size_t section = opt + 0xf0;
    unsigned char *text = pe + PE_TEXT_RAW;

    memset( pe, 0, PE_FILE_SIZE );

    put16( pe, 0x5a4d );
    put32( pe + 0x3c, pe_offset );

    put32( pe + pe_offset, 0x00004550 );
    put16( pe + coff, 0xaa64 );
    put16( pe + coff + 2, 1 );
    put16( pe + coff + 16, 0xf0 );
    put16( pe + coff + 18, 0x0022 );

    put16( pe + opt, 0x20b );
    pe[opt + 2] = 1;
    put32( pe + opt + 4, PE_TEXT_RAW_SIZE );
    put32( pe + opt + 16, PE_TEXT_RVA );
    put32( pe + opt + 20, PE_TEXT_RVA );
    put64( pe + opt + 24, PE_IMAGE_BASE );
    put32( pe + opt + 32, 0x1000 );
    put32( pe + opt + 36, 0x200 );
    put16( pe + opt + 40, 6 );
    put16( pe + opt + 48, 6 );
    put32( pe + opt + 56, PE_IMAGE_SIZE );
    put32( pe + opt + 60, PE_HEADER_SIZE );
    put16( pe + opt + 68, 3 );
    put64( pe + opt + 72, 0x100000 );
    put64( pe + opt + 80, 0x1000 );
    put64( pe + opt + 88, 0x100000 );
    put64( pe + opt + 96, 0x1000 );
    put32( pe + opt + 108, 16 );

    memcpy( pe + section, ".text", 5 );
    put32( pe + section + 8, 12 );
    put32( pe + section + 12, PE_TEXT_RVA );
    put32( pe + section + 16, PE_TEXT_RAW_SIZE );
    put32( pe + section + 20, PE_TEXT_RAW );
    put32( pe + section + 36, 0x60000020 );

    put32( text, arm64_movz_w( 0, PE_ENTRY_RESULT & 0xffff, 0 ) );
    put32( text + 4, arm64_movk_w( 0, PE_ENTRY_RESULT >> 16, 16 ) );
    put32( text + 8, 0xd65f03c0U );
}

static int rva_to_file_offset( const unsigned char *pe, size_t pe_size, uint32_t rva, uint32_t *file_offset )
{
    uint32_t pe_offset = get32( pe + 0x3c );
    const unsigned char *coff = pe + pe_offset + 4;
    const unsigned char *section = coff + 20 + get16( coff + 16 );
    unsigned int section_count = get16( coff + 2 );
    unsigned int i;

    if (rva < PE_HEADER_SIZE)
    {
        *file_offset = rva;
        return rva < pe_size;
    }

    for (i = 0; i < section_count; i++)
    {
        const unsigned char *hdr = section + i * 40;
        uint32_t virtual_size = get32( hdr + 8 );
        uint32_t virtual_address = get32( hdr + 12 );
        uint32_t raw_size = get32( hdr + 16 );
        uint32_t raw_ptr = get32( hdr + 20 );
        uint32_t span = virtual_size > raw_size ? virtual_size : raw_size;

        if (rva >= virtual_address && rva < virtual_address + span)
        {
            *file_offset = raw_ptr + (rva - virtual_address);
            return *file_offset < pe_size;
        }
    }

    return 0;
}

static int apply_base_relocs( unsigned char *image, size_t image_size,
                              const unsigned char *pe, size_t pe_size,
                              uint64_t preferred_base, uint64_t actual_base,
                              uint32_t reloc_rva, uint32_t reloc_size )
{
    int64_t delta = (int64_t)(actual_base - preferred_base);
    uint32_t pos_rva = reloc_rva;
    uint32_t end_rva = reloc_rva + reloc_size;

    if (!reloc_rva || !reloc_size || !delta) return 1;

    while (pos_rva < end_rva)
    {
        uint32_t block_offset;
        uint32_t page_rva;
        uint32_t block_size;
        uint32_t entries;
        uint32_t i;

        if (!rva_to_file_offset( pe, pe_size, pos_rva, &block_offset ) || block_offset + 8 > pe_size) return 0;
        page_rva = get32( pe + block_offset );
        block_size = get32( pe + block_offset + 4 );
        if (block_size < 8 || pos_rva + block_size > end_rva) return 0;

        entries = (block_size - 8) / 2;
        for (i = 0; i < entries; i++)
        {
            uint16_t entry = get16( pe + block_offset + 8 + i * 2 );
            uint16_t type = entry >> 12;
            uint16_t offset = entry & 0x0fff;
            uint32_t target_rva = page_rva + offset;

            if (type == 0) continue;
            if (type != 10 || target_rva + 8 > image_size) return 0;
            put64( image + target_rva, get64( image + target_rva ) + delta );
        }
        pos_rva += block_size;
    }

    return 1;
}

static uint32_t run_pe_image( const unsigned char *pe, size_t pe_size )
{
    uint32_t pe_offset;
    const unsigned char *coff;
    const unsigned char *opt;
    const unsigned char *section;
    unsigned int section_count;
    uint16_t optional_size;
    uint32_t entry_rva;
    uint64_t image_base;
    uint32_t image_size;
    uint32_t header_size;
    uint32_t reloc_rva;
    uint32_t reloc_size;
    unsigned char *image;
    unsigned int i;
    uint32_t (*entry)(void);
    uint32_t result;

    if (pe_size < PE_HEADER_SIZE || get16( pe ) != 0x5a4d) return 0xffffffffU;
    pe_offset = get32( pe + 0x3c );
    if (pe_offset + 4 + 20 > pe_size || get32( pe + pe_offset ) != 0x00004550) return 0xfffffffeU;

    coff = pe + pe_offset + 4;
    optional_size = get16( coff + 16 );
    opt = coff + 20;
    section = opt + optional_size;
    section_count = get16( coff + 2 );

    if (get16( coff ) != 0xaa64 || get16( opt ) != 0x20b) return 0xfffffffdU;
    if (optional_size < 0xf0 || section + section_count * 40 > pe + pe_size) return 0xfffffffcU;

    entry_rva = get32( opt + 16 );
    image_base = get64( opt + 24 );
    image_size = get32( opt + 56 );
    header_size = get32( opt + 60 );
    reloc_rva = get32( opt + 112 + 5 * 8 );
    reloc_size = get32( opt + 112 + 5 * 8 + 4 );

    log_line( "[PE] machine=ARM64 sections=%u image_size=0x%x entry_rva=0x%x", section_count, image_size, entry_rva );

    image = horizon_mmap( NULL, image_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0 );
    if (image == MAP_FAILED)
    {
        log_line( "[FAIL] mmap image errno=%d", errno );
        failures++;
        return 0xfffffffbU;
    }

    memset( image, 0, image_size );
    memcpy( image, pe, header_size < pe_size ? header_size : pe_size );

    for (i = 0; i < section_count; i++)
    {
        const unsigned char *hdr = section + i * 40;
        uint32_t virtual_size = get32( hdr + 8 );
        uint32_t virtual_address = get32( hdr + 12 );
        uint32_t raw_size = get32( hdr + 16 );
        uint32_t raw_ptr = get32( hdr + 20 );

        if (virtual_address + virtual_size > image_size ||
            virtual_address + raw_size > image_size ||
            raw_ptr + raw_size > pe_size)
        {
            log_line( "[FAIL] invalid section %u", i );
            failures++;
            horizon_munmap( image, image_size );
            return 0xfffffffaU;
        }

        memcpy( image + virtual_address, pe + raw_ptr, raw_size );
        log_line( "[PE] section %.8s va=0x%x raw=0x%x", hdr, virtual_address, raw_size );
    }

    if (!apply_base_relocs( image, image_size, pe, pe_size, image_base,
                            (uint64_t)(uintptr_t)image, reloc_rva, reloc_size ))
    {
        log_line( "[FAIL] relocations" );
        failures++;
        horizon_munmap( image, image_size );
        return 0xfffffff9U;
    }

    if (horizon_mprotect( image, image_size, PROT_READ | PROT_EXEC ) == -1)
    {
        log_line( "[FAIL] mprotect RX errno=%d", errno );
        failures++;
        horizon_munmap( image, image_size );
        return 0xfffffff8U;
    }

    entry = (uint32_t (*)(void))(void *)(image + entry_rva);
    result = entry();
    check_bool( "PE entry returned expected value", result == PE_ENTRY_RESULT );

    horizon_munmap( image, image_size );
    return result;
}

static void park_forever(void)
{
    log_line( "[EXIT] parked after summary; close the application from HOME" );
    for (;;) svcSleepThread( 1000000000LL );
}

int main(int argc, char **argv)
{
    unsigned char pe[PE_FILE_SIZE];
    FILE *pe_file;
    uint32_t result;

    (void)argc;
    (void)argv;

    consoleInit( NULL );
    mkdir( "sdmc:/switch", 0777 );
    mkdir( "sdmc:/switch/wine-nx-probe", 0777 );
    log_file = fopen( "sdmc:/switch/wine-nx-probe/pe-smoke.log", "w" );

    log_line( "wine-nx-pe-smoke: importless ARM64 PE loader smoke" );
    build_importless_arm64_pe( pe );

    pe_file = fopen( "sdmc:/switch/wine-nx-probe/hello-arm64-importless.exe", "wb" );
    if (pe_file)
    {
        fwrite( pe, 1, sizeof(pe), pe_file );
        fclose( pe_file );
        log_line( "[OK] wrote hello-arm64-importless.exe" );
    }
    else log_line( "[INFO] could not write PE file errno=%d", errno );

    check_bool( "MZ header", get16( pe ) == 0x5a4d );
    check_bool( "PE signature", get32( pe + get32( pe + 0x3c ) ) == 0x00004550 );

    result = run_pe_image( pe, sizeof(pe) );
    log_line( "[PE] entry result=0x%08x", result );
    log_line( "SUMMARY failures=%d overall=%s", failures, failures ? "FAIL" : "OK" );
    park_forever();
    return failures ? 1 : 0;
}
