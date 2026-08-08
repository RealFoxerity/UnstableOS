#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <string.h>

#include <ctype.h>
#include <signal.h>
#include <time.h>

int fd = -1;
static void print_help(char * argv0, char print_vga, char print_mh) {
    fprintf(stderr,
"Usage:\n"
" %s [gGsSWBcCdDlLpamMz]\n"
"\n"
"\"zamm! rezoultion setter\" display modesetting utility\n"
"\n"
"Generic options:\n"
"\t-h          This help message\n"
"\t-H          Also print modeline format help\n"
"\t-V          Also print VGA options\n"
"\t-g          Get the current mode and modeline information\n"
"\t-G          Get all supported modes\n"
"\t-s [id]     Set a mode based on id from -G\n"
"\t-S [fmt]    Try to set a modeline\n"
"\t-W          Wait 5s for confirm, otherwise revert\n"
, argv0);
if (print_vga)
    fprintf(stderr,
"\nVGA-only options:\n"
"Last argument takes precedence\n"
"\t-B          Do bounds checking on modelines\n"
"\t-c          Try to set the hi-color mode (256 colors)\n"
"\t-C             ... unset ... \n"
"\t-d          Try to set scan doubling\n"
"\t-D             ... unset ...\n"
"\t-l          Try to set clock halving\n"
"\t-L             ... unset ...\n"
"\t-p [N]      Try to set data per scanline to N dots\n"
"\t-a [N]      Try to set pixels per address to N\n"
"\t-m [mode]   Try to set addressing mode to B/W/D\n"
"\t-M [amode]  Try to set access mode to C/U/M\n"
"\t-z [WxH]    Try to set the fb console rezoultion\n"
);
if (print_mh)
    fprintf(stderr,
"\nModeline format:\n"
"\tW,FP,SW,BP,+/-,H,FP,SW,BP,+/-,O,CLK\n"
"\tW   - width\n"
"\tFP  - front porch\n"
"\tSW  - hsync width\n"
"\tBP  - back porch\n"
"\t+/- - hsync polarity\n"
"\tH   - height\n"
"\tFP  - front porch\n"
"\tSW  - vsync width\n"
"\tBP  - back porch\n"
"\t+/- - vsync polarity\n"
"\tO   - overscan\n"
"\tCLK - display/dot clock in kHz\n"
"\t      VGA only supports 3 values:\n"
"\t      \t0 - DOT8 - 25MHz\n"
"\t      \t1 - DOT9 - 28MHz\n"
"\t      \t2 - Paradise/Hercules - 40MHz\n"
"\t      \t        Only on certain cards\n"
    );

    // will cause -EFAULT on VGA, and -EINVAL on others
    ioctl(fd, FB_GET_VGA_MISC, NULL);
    if (errno != EFAULT) {
        fprintf(stderr, "Note: device doesn't seem to support VGA options\n");
    }
}

static const char valid_options[] = ":hHVgGs:S:WBcCdDlLp:a:m:M:z:";

static void print_mode(struct fb_info fi) {
    char types[3] = "LPU";
    printf("[ %hu ] - %c - w %lu h %lu pn %hu bpp %hhu\n",
        fi.id, types[fi.type & 3], fi.width, fi.height, fi.planes, fi.bpp);
}

static void print_modeline(struct vesa_modeline modeline) {
    printf("%u,%u,%u,%u,%c,%u,%u,%u,%u,%c,%hhu,%lu\n",
        modeline.horizontal,
        modeline.horizontal_fporch,
        modeline.horizontal_sync,
        modeline.horizontal_bporch,
        modeline.hsync_polarity ? '+' : '-',
        modeline.vertical,
        modeline.vertical_fporch,
        modeline.vertical_sync,
        modeline.vertical_bporch,
        modeline.vsync_polarity ? '+' : '-',
        modeline.overscan,
        modeline.clock_khz
    );
}

void dummy_handler(int _) {}
int main(int argc, char ** argv) {
    fd = open("/dev/fb0", O_RDONLY);
    if (fd == -1) {
        fprintf(stderr, "%s: Failed to open framebuffer: %s!\n", argv[0], strerror(errno));
        return -1;
    }

    if (argc < 2) {
        print_help(argv[0], 0, 0);
        return 0;
    }

    signal(SIGALRM, dummy_handler);

    char opt;
    char help = 0;
    char do_bounds = 0;
    char get_mode = 0; // 1 = get current, 2 = get all
    char set_mode = 0; // 1 = set by id, 2 = set modeline
    char set_vm = 0;
    unsigned long set_mode_id = 0;

    struct vesa_modeline modeline = {0};

    char wait = 0;
    struct vga_misc_params vm = {0};


    // if this fails, then set will as well, and we don't care
    ioctl(fd, FB_GET_VGA_MISC, &vm);
    struct vga_misc_params vm_orig = vm;

    while ((opt = getopt(argc, argv, valid_options)) != -1) {
        char * end = optarg;
        switch (opt) {
            case 'h':
                help |= 1;
                break;
            case 'H':
                help |= 2;
                break;
            case 'V':
                help |= 4;
                break;
            case 'g':
                get_mode |= 1;
                break;
            case 'G':
                get_mode |= 2;
                break;
            case 's':
                if (set_mode == 2) {
                    fprintf(stderr, "Options -s and -S are mutually exclusive\n");
                    return 1;
                }
                set_mode_id = strtoul(optarg, &end, 0);
                if (*end != '\0') {
                    fprintf(stderr, "Invalid mode id %s\n", optarg);
                    return 1;
                }
                set_mode = 1;
                break;
            case 'S':
                if (set_mode == 1) {
                    fprintf(stderr, "Options -s and -S are mutually exclusive\n");
                    return 1;
                }
                char hsync_pol, vsync_pol;
                if (sscanf(optarg, "%u,%u,%u,%u,%c,%u,%u,%u,%u,%c,%hhu,%lu",
                    &modeline.horizontal,
                    &modeline.horizontal_fporch,
                    &modeline.horizontal_sync,
                    &modeline.horizontal_bporch,
                    &hsync_pol,
                    &modeline.vertical,
                    &modeline.vertical_fporch,
                    &modeline.vertical_sync,
                    &modeline.vertical_bporch,
                    &vsync_pol,
                    &modeline.overscan,
                    &modeline.clock_khz) != 12 ||
                    (hsync_pol != '+' && hsync_pol != '-') ||
                    (vsync_pol != '+' && vsync_pol != '-')) {
                    fprintf(stderr, "Invalid modeline %s\n", optarg);
                    return 1;
                }
                modeline.hsync_polarity = hsync_pol == '+'?1:0;
                modeline.vsync_polarity = vsync_pol == '+'?1:0;
                set_mode = 2;
                break;
            case 'W':
                wait = 1;
                break;

            // VGA specific
            case 'B':
                do_bounds = 1;
                break;
            case 'c':
                set_vm = 1;
                vm.hicolor = 1;
                break;
            case 'C':
                set_vm = 1;
                vm.hicolor = 0;
                break;
            case 'd':
                set_vm = 1;
                vm.scan_doubling = 1;
                break;
            case 'D':
                set_vm = 1;
                vm.scan_doubling = 0;
                break;
            case 'l':
                set_vm = 1;
                vm.clock_halving = 1;
                break;
            case 'L':
                set_vm = 1;
                vm.clock_halving = 0;
                break;
            case 'p':
                set_vm = 1;
                vm.data_per_scanline = strtoul(optarg, &end, 0);
                if (*end != '\0') {
                    fprintf(stderr, "Invalid data per scanline %s\n", optarg);
                    return 1;
                }
                break;
            case 'a':
                set_vm = 1;
                vm.pixels_per_address = strtoul(optarg, &end, 0);
                if (*end != '\0') {
                    fprintf(stderr, "Invalid pixels per address %s\n", optarg);
                    return 1;
                }
                break;
            case 'm':
                set_vm = 1;
                if (optarg[1] != '\0') {
                    inv_addr_m:
                    fprintf(stderr, "Invalid addressing mode %s\n", optarg);
                    return 1;
                }
                switch (tolower(optarg[0])) {
                    case 'b':
                        vm.addressing_mode = VGA_AM_BYTE;
                        break;
                    case 'w':
                        vm.addressing_mode = VGA_AM_WORD;
                        break;
                    case 'd':
                        vm.addressing_mode = VGA_AM_DWORD;
                        break;
                    default: goto inv_addr_m;
                }
                break;
            case 'M':
                set_vm = 1;
                if (optarg[1] != '\0') {
                    inv_acc_m:
                    fprintf(stderr, "Invalid access mode %s\n", optarg);
                    return 1;
                }
                switch (tolower(optarg[0])) {
                    case 'c':
                        vm.access_mode = CHAINED;
                        break;
                    case 'u':
                        vm.access_mode = UNCHAINED;
                        break;
                    case 'm':
                        vm.access_mode = MODE12;
                        break;
                    default: goto inv_acc_m;
                }
                break;
            case 'z':
                set_vm = 1;
                if (sscanf(optarg, "%hux%hu", &vm.actual_width, &vm.actual_height) != 2) {
                    fprintf(stderr, "Invalid width or height\n");
                    return 1;
                }
                break;
            case ':':
                fprintf(stderr, "Missing argument for -%c\n", optopt);
                print_help(argv[0], 0, 0);
                return 1;
            case '?':
            default:
                fprintf(stderr, "Invalid option -%c\n", optopt);
                print_help(argv[0], 0, 0);
                return 1;
        }
    }

    if (help) {
        print_help(argv[0], help & 0b100, help & 0b010);
        return 0;
    }

    struct fb_info fi = {0};
    if (ioctl(fd, FB_GET_ACTIVE_MODE, &fi) == -1) {
        fprintf(stderr, "%s: Failed to read current mode: %s!\n", argv[0], strerror(errno));
        return -1;
    }

    struct vesa_modeline orig_modeline = {0};
    if (ioctl(fd, FB_GET_MODELINE, &orig_modeline) == -1) {
        fprintf(stderr, "%s: Failed to get modeline: %s!\n", argv[0], strerror(errno));
        return -1;
    }

    if (get_mode & 1) {
        printf("Currently active mode:\n");
        print_mode(fi);
        printf("\tModeline:\n");
        print_modeline(orig_modeline);
    }
    if (get_mode & 2) {
        size_t mode_count = ioctl(fd, FB_GET_MODES, NULL);
        if (mode_count == -1) {
            frml:
            fprintf(stderr, "%s: Failed to read mode list: %s!\n", argv[0], strerror(errno));
            return -1;
        }
        struct fb_info * modes = malloc(mode_count * sizeof(struct fb_info));
        if (modes == NULL) {
            fprintf(stderr, "%s: Failed to allocate mode list: %s!\n", argv[0], strerror(errno));
            return -1;
        }
        if (ioctl(fd, FB_GET_MODES, modes) == -1)
            goto frml;

        printf("Supported modes:\n");
        for (size_t i = 0; i < mode_count; i++) {
            print_mode(modes[i]);
        }
    }

    if (set_mode == 1) {
        if (ioctl(fd, FB_SET_MODE, set_mode_id) == -1) {
            fprintf(stderr, "%s: Failed to set mode %lu: %s!\n", argv[0], set_mode_id, strerror(errno));
            return -1;
        }
    }

    if (set_mode == 2) {
        if (ioctl(fd, FB_SET_MODELINE, &modeline) == -1) {
            fprintf(stderr, "%s: Failed to set modeline: %s!\n", argv[0], strerror(errno));
            return -1;
        }
    }

    if (set_vm) ioctl(fd, FB_SET_VGA_MISC, &vm);

    if (wait) {
        printf("Is this mode correct? [y/N]\n");
        alarm(5);
        if (tolower(fgetc(stdin)) != 'y') {
            ioctl(fd, FB_SET_VGA_MISC, &vm_orig);
            if (set_mode == 1) ioctl(fd, FB_SET_MODE, fi.id);
            if (set_mode == 2) ioctl(fd, FB_SET_MODELINE, &orig_modeline);
            return 0;
        }
    }

    return 0;
}