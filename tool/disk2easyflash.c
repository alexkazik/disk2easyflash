#include <stdlib.h>
#include <stdio.h>
#include <getopt.h>
#include <errno.h>
#include <libgen.h>
#include <sys/stat.h>
#include <dirent.h>
#include <string.h>

#include "m2i.h"
#include "d64.h"
#include "bundle.h"
#include "binaries.h"
#include "cart.h"

#define mode_lo 0
#define mode_nm 1
#define mode_hi 2

#define build_none 0
#define build_xbank 1
#define build_crt 2
#define build_list 3

char *progname;
int do_ignore[256], ignore_cnt = 0, do_remove[256], remove_cnt = 0;

int longopt;
static struct option longopts[] = {
  {"low",         no_argument,       &longopt, 'l'},
  {"normal",      no_argument,       &longopt, 'n'},
  {"high",        no_argument,       &longopt, 'h'},
  {"xbank",       no_argument,       NULL,     'x'},
  {"crt",         no_argument,       NULL,     'c'},
  {"list",        no_argument,       NULL,     'l'},
  {"remove",      required_argument, NULL,     'r'},
  {"ignore",      required_argument, NULL,     'i'},
  {"nolisting",   no_argument,       NULL,     'n'},
  {"verbose",     no_argument,       NULL,     'v'},
  {"blocks-free", required_argument, &longopt, 'b'},
  {NULL,          0,                 NULL,     0}
};

void usage() {
  fprintf(stderr, "\n*** DISK2EASYFLASH V0.92 ***\n\n");
  fprintf(stderr, "Usage: %s --crt|--xbank [<options>] <d64/m2i-file/m2i-dir> <output file>\n", progname);
  fprintf(stderr, "Usage: %s --list [<options>] <d64/m2i-file/m2i-dir>\n", progname);
  fprintf(stderr, "  --crt, -c = build an easyflash cartridge\n");
  fprintf(stderr, "  --xbank, -x = build an easyflash xbank cartridge\n");
  fprintf(stderr, "  --list, -l = just list the contents of the d64/m2i\n");
  fprintf(stderr, "  --remove <id>, -r <id> = remove the specified entry from the listing\n");
  fprintf(stderr,
          "  --ignore <id>, -i <id> = don't put that file in the easyflash, but in the listing (for savegames)\n");
  fprintf(stderr, "the following commands are only used in crt/xbank mode\n");
  fprintf(stderr, "  --normal = create a cartridge, using the full 16 KiB banks (default)\n");
  fprintf(stderr, "  --low = create a cartridge, using only the lower 8 KiB banks\n");
  fprintf(stderr, "  --high = create a cartridge, using only the upper 8 KiB banks\n");
  fprintf(stderr, "  --nolisting, -n = don't add the listing (\"$\")\n");
  fprintf(stderr, "  --blocks-free <num> = how many blocks should be displayed as free (default: 2)\n");
  fprintf(stderr, "  --verbose, -v = print out the listing\n");
  exit(1);
}

char cart_singnature[] = CART_SIGNATURE;
char chip_singnature[] = CHIP_SIGNATURE;

char txt_blocks_free[] = "BLOCKS FREE.";
char txt_prg[] = "PRG";
char txt_del[] = "DEL";

int main(int argc, char **argv) {
  int ch, i, is_m2i;
  int mode = mode_nm;
  int build = build_none;
  int nolisting = 0;
  int blocksfree = 2;
  int verbose = 0;
  struct stat m2i_stat;
  struct m2i *entries, *entr;
  FILE *out;
  CartHeader CartHeader;

  progname = argv[0];

  while ((ch = getopt_long(argc, argv, "xclr:i:nv", longopts, NULL)) != -1) {
    switch (ch) {
      case 'x':
        if (build != build_none) {
          usage();
        }
        build = build_xbank;
        break;
      case 'c':
        if (build != build_none) {
          usage();
        }
        build = build_crt;
        break;
      case 'l':
        if (build != build_none) {
          usage();
        }
        build = build_list;
        break;
      case 'r':
        do_remove[remove_cnt++] = strtol(optarg, NULL, 10);
        break;
      case 'i':
        do_ignore[ignore_cnt++] = strtol(optarg, NULL, 10);
        break;
      case 0:
        switch (longopt) {
          case 'l':
            mode = mode_lo;
            break;
          case 'n':
            mode = mode_nm;
            break;
          case 'h':
            mode = mode_hi;
            break;
          case 'b':
            blocksfree = strtol(optarg, NULL, 10);;
            break;
        }
        break;
      case 'n':
        nolisting = 1;
        break;
      case 'v':
        verbose = 1;
        break;
      case '?':
        usage();
        break;
    }
  }

  if (build == build_none) {
    usage();
  }

  argc -= optind;
  argv += optind;

  if (!(argc == 2 && build != build_list) && !(argc == 1 && build == build_list)) {
    usage();
  }

  stat(argv[0], &m2i_stat);
  if ((m2i_stat.st_mode & S_IFMT) == S_IFDIR) {
    // m2i is a dir -> find the first m2i in that dir
    struct dirent *entry;
    char buffer[1000];
    DIR *m2idir = opendir(argv[0]);


    if (!m2idir) {
      usage();
    }
    while (entry = readdir(m2idir)) {
      int d_namlen = strlen(entry->d_name);
      if (d_namlen > 4 &&
          (strcmp(&entry->d_name[d_namlen - 4], ".m2i") == 0 || strcmp(&entry->d_name[d_namlen - 4], ".M2I") == 0)) {
        break;
      }
    }
    closedir(m2idir);
    if (!entry) {
      usage();
    }

    strcpy(buffer, argv[0]);
    if (buffer[strlen(buffer) - 1] != '/') {
      strcat(buffer, "/");
    }
    strcat(buffer, entry->d_name);
    argv[0] = malloc(strlen(buffer) + 1);
    strcpy(argv[0], buffer);

    is_m2i = 1;
  } else {
    // m2i should be a file, at least treat it as one, may fail
    is_m2i = strlen(argv[0]) > 4 &&
             (strcmp(&argv[0][strlen(argv[0]) - 4], ".m2i") == 0 || strcmp(&argv[0][strlen(argv[0]) - 4], ".M2I") == 0);
  }

  if (is_m2i) {
    entries = parse_m2i(argv[0]);
  } else {
    entries = parse_d64(argv[0]);
  }

  for (i = 0; i < remove_cnt; i++) {
    for (ch = 1, entr = entries->next; entr != NULL; ch++, entr = entr->next) {
      if (do_remove[i] == ch && entr->type != '*') {
        entr->type = '*';
        break;
      }
    }
    if (entr == NULL) {
      fprintf(stderr, "unable to remove entry %d, because there is no such one", do_remove[i]);
      exit(1);
    }
  }

  for (i = 0; i < ignore_cnt; i++) {
    for (ch = 1, entr = entries->next; entr != NULL; ch++, entr = entr->next) {
      if (do_ignore[i] == ch && entr->type == 'p') {
        entr->type = 'q';
        break;
      }
    }
    if (entr == NULL) {
      fprintf(stderr, "unable to ignore entry %d, because there is no such one", do_remove[i]);
      exit(1);
    }
  }

  for (entr = entries->next; entr != NULL; entr = entr->next) {
    if (entr->type == 'p' && entr->length < 3) {
      fprintf(stderr, "below minimum file length of 3: \"%s\"\n", entr->name);
      exit(1);
    }
  }


  if (!nolisting) {
    struct m2i *entry = malloc(sizeof(struct m2i));
    int num;

    // search last entry
    for (num = 0, entr = entries; entr != NULL; num++, entr = entr->next);

    // fill up entry
    entry->next = NULL;
    strcpy(entry->name, "$");
    entry->type = 'p';
    entry->data = malloc(50 + num * 30);
    entry->length = 0;

    // basic setup
    entr = entries;

    entry->data[entry->length++] = 0x01; // laod address
    entry->data[entry->length++] = 0x04;

    // first line
    entry->data[entry->length++] = 0x01; // next ptr
    entry->data[entry->length++] = 0x01;
    entry->data[entry->length++] = 0x00; // size
    entry->data[entry->length++] = 0x00;
    entry->data[entry->length++] = 0x12; // REVERSE
    entry->data[entry->length++] = 0x22; // QUOTE
    for (i = 0; i < strlen(entr->name); i++) {
      entry->data[entry->length++] = entr->name[i]; // name
    }
    for (; i < 16; i++) {
      entry->data[entry->length++] = 0x20; // SPACE
    }
    entry->data[entry->length++] = 0x22; // QUOTE
    entry->data[entry->length++] = 0x20; // SPACE
    for (i = 0; i < 5; i++) {
      entry->data[entry->length++] = entr->id[i]; // name
    }
    entry->data[entry->length++] = 0x00; // END OF LINE

    for (entr = entries->next; entr != NULL; entr = entr->next) {
      char *type = entr->type == 'd' ? txt_del : txt_prg;
      int size = (entr->length + 253) / 254;

      // entry
      entry->data[entry->length++] = 0x01; // next ptr
      entry->data[entry->length++] = 0x01;
      entry->data[entry->length++] = size; // size
      entry->data[entry->length++] = size >> 8;

      // convert size -> 4-strlen("size")
      if (size < 10) {
        size = 3;
      } else if (size < 100) {
        size = 2;
      } else {
        size = 1;
      }

      for (i = 0; i < size; i++) {
        entry->data[entry->length++] = 0x20; // SPACE
      }

      entry->data[entry->length++] = 0x22; // QUOTE
      for (i = 0; i < strlen(entr->name); i++) {
        entry->data[entry->length++] = entr->name[i]; // name
      }
      entry->data[entry->length++] = 0x22; // QUOTE
      for (; i < 17; i++) {
        entry->data[entry->length++] = 0x20; // SPACE
      }
      for (i = 0; i < 3; i++) {
        entry->data[entry->length++] = type[i]; // type (PRG/DEL)
      }
      entry->data[entry->length++] = 0x00; // END OF LINE

    }

    // last line
    entry->data[entry->length++] = 0x01; // next ptr
    entry->data[entry->length++] = 0x01;
    entry->data[entry->length++] = blocksfree; // size
    entry->data[entry->length++] = blocksfree >> 8;
    for (i = 0; i < strlen(txt_blocks_free); i++) {
      entry->data[entry->length++] = txt_blocks_free[i]; // name
    }
    entry->data[entry->length++] = 0x00; // END OF LINE

    // end of file
    entry->data[entry->length++] = 0x00; // END OF FILE
    entry->data[entry->length++] = 0x00;

    // search last entry, add
    for (entr = entries; entr->next != NULL; entr = entr->next);
    entr->next = entry;

  }


  if (build == build_list || verbose) {
    char quotename[19];
    int num;

    printf("      | 0 \"%-16s\" %s |\n", entries->name, entries->id);

    for (num = 1, entr = entries->next; nolisting ? (entr != NULL) : (entr->next != NULL); num++, entr = entr->next) {
      if (entr->type == '*' || (entr->type != 'p' && nolisting)) {
        continue;
      }

      //		printf("%c:%s:%d\n", entr->type, entr->name, entr->length);

      sprintf(quotename, "\"%s\"", entr->name);

      /* Print directory entry */
      printf("%4d  | %-4d  %-18s %s |  %5d Bytes%s\n", num, (entr->length + 253) / 254, quotename,
             entr->type == 'd' ? "DEL" : "PRG", entr->length,
             entr->type == 'q' ? ", not in flash, only in listing" : "");

    }

    if (build != build_list && entr != NULL) {
      // the listing
      printf("      |       The Listing (\"$\")      |  %5d Bytes\n", entr->length);
    }

  }

  if (build != build_list) {

    out = fopen(argv[1], "wb");
    if (!out) {
      fprintf(stderr, "unable to open \"%s\" for output", argv[1]);
      exit(1);
    }

    memcpy(CartHeader.signature, cart_singnature, 16);
    CartHeader.headerLen[0] = 0x00;
    CartHeader.headerLen[1] = 0x00;
    CartHeader.headerLen[2] = 0x00;
    CartHeader.headerLen[3] = 0x40;
    CartHeader.version[0] = 0x01;
    CartHeader.version[1] = 0x00;
    if (build == build_xbank) {
      CartHeader.type[0] = CART_TYPE_EASYFLASH_XBANK >> 8;
      CartHeader.type[1] = CART_TYPE_EASYFLASH_XBANK & 0xff;
      CartHeader.exromLine = (mode == mode_hi) ? 1 : 0;
      CartHeader.gameLine = (mode == mode_hi) ? 0 : ((mode == mode_lo) ? 1 : 0);
    } else {
      CartHeader.type[0] = CART_TYPE_EASYFLASH >> 8;
      CartHeader.type[1] = CART_TYPE_EASYFLASH & 0xff;
      CartHeader.exromLine = 1;
      CartHeader.gameLine = 0;
    }
    CartHeader.reserved[0] = 0;
    CartHeader.reserved[1] = 0;
    CartHeader.reserved[2] = 0;
    CartHeader.reserved[3] = 0;
    CartHeader.reserved[4] = 0;
    CartHeader.reserved[5] = 0;
    memset(CartHeader.name, 0, 32);
    strncpy(CartHeader.name, basename(argv[0]), 32);

    fwrite(&CartHeader, 1, sizeof(CartHeader), out);

    if (build == build_crt) {
      char buffer[0x2000];
      BankHeader BankHeader;

      memset(buffer, 0xff, 0x2000);
      memcpy(buffer + 0x1800, sprites, sprites_size);
      memcpy(buffer + 0x1e00, startup, startup_size);
      buffer[0x2000 - startup_size + 0] = 1; // BANK
      switch (mode) {
        case mode_nm:
          buffer[0x2000 - startup_size + 1] = 7; // MODE
          break;
        case mode_lo:
          buffer[0x2000 - startup_size + 1] = 6; // MODE
          break;
        case mode_hi:
          buffer[0x2000 - startup_size + 1] = 5; // MODE
          break;
      }
      // setup commons
      memcpy(BankHeader.signature, chip_singnature, 4);
      BankHeader.packetLen[0] = 0x00;
      BankHeader.packetLen[1] = 0x00;
      BankHeader.packetLen[2] = 0x20;
      BankHeader.packetLen[3] = 0x10;
      BankHeader.chipType[0] = 0x00;
      BankHeader.chipType[1] = 0x00;
      BankHeader.bank[0] = 0x00;
      BankHeader.bank[1] = 0x00;
      BankHeader.loadAddr[0] = 0xa0;
      BankHeader.loadAddr[1] = 0x00;
      BankHeader.romLen[0] = 0x20;
      BankHeader.romLen[1] = 0x00;

      fwrite(&BankHeader, 1, sizeof(BankHeader), out);

      fwrite(buffer, 1, 0x2000, out);
    }

    switch (mode) {
      case mode_nm:
        bundle(out, entries, 0x4000, 0x8000, 14, kapi_nm, kapi_nm_size, NULL, 0, build == build_xbank ? 0 : 1);
        break;
      case mode_lo:
        bundle(out, entries, 0x2000, 0x8000, 13, kapi_lo, kapi_lo_size, NULL, 0, build == build_xbank ? 0 : 1);
        break;
      case mode_hi:
        bundle(out, entries, 0x2000, 0xa000, 13, kapi_hi, kapi_hi_size, launcher_hi, launcher_hi_size,
               build == build_xbank ? 0 : 1);
        break;
    }

  }

  exit(0);
}
