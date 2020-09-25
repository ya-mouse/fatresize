/*
 * Copyright (C) 2005-2020  Anton D. Kachalov <mouse@ya.ru>
 *
 * The FAT16/FAT32 non-destructive resizer.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/* activate PED_ASSERT */
#ifndef DEBUG
#define DEBUG 1
#endif

#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include <inttypes.h>
#include <limits.h>
#include <parted/debug.h>
#include <parted/parted.h>
#include <parted/unit.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include "config.h"

#define FAT_ASSERT(cond, action) \
  do {                           \
    if (!(cond)) {               \
      PED_ASSERT(cond);          \
      action;                    \
    }                            \
  } while (0)

#define MAX_SIZE_STR "max"

static struct {
  unsigned char *fullpath;
  unsigned char *device;
  int pnum;
  PedSector size;
  int verbose;
  int progress;
  int info;
  int force_yes;
} opts;

typedef struct {
  time_t last_update;
  time_t predicted_time_left;
} TimerContext;

static TimerContext timer_context;

static void usage(int code) {
  fprintf(stdout,
          "Usage: %s [options] device (e.g. /dev/hda1, /dev/sda2)\n"
          "    Resize an FAT16/FAT32 volume non-destructively:\n\n"
          "    -s, --size SIZE      Resize volume to SIZE[k|M|G|ki|Mi|Gi] bytes "
          "or \"" MAX_SIZE_STR
          "\"\n"
          "    -i, --info           Show volume information\n"
          "    -f, --force-yes      Do not ask questions\n"
          "    -n, --partition NUM  Specify partition number\n"
          "    -p, --progress       Show progress\n"
          "    -q, --quiet          Be quiet\n"
          "    -v, --verbose        Verbose\n"
          "    -h, --help           Display this help\n\n"
          "Please report bugs to %s\n",
          PACKAGE_NAME, PACKAGE_BUGREPORT);

  exit(code);
}

static void printd(int level, const char *fmt, ...) {
  va_list ap;

  if (opts.verbose < level) {
    return;
  }

  va_start(ap, fmt);
  vprintf(fmt, ap);
  va_end(ap);
}

static PedSector get_size(char *s) {
  PedSector size;
  char *suffix;
  int prefix_kind = 1000;

  if (!strncmp(s, MAX_SIZE_STR, sizeof(MAX_SIZE_STR) - 1)) return LLONG_MAX;

  size = strtoll(s, &suffix, 10);
  if (size <= 0 || errno == ERANGE) {
    fprintf(stderr, "Illegal new volume size\n");
    usage(1);
  }

  if (!*suffix) {
    return size;
  }

  if (strlen(suffix) == 2 && suffix[1] == 'i') {
    prefix_kind = 1024;
  } else if (strlen(suffix) > 1) {
    usage(1);
  }

  switch (*suffix) {
    case 'G':
      size *= prefix_kind;
    case 'M':
      size *= prefix_kind;
    case 'k':
      size *= prefix_kind;
      break;
    default:
      usage(1);
  }

  return size;
}

/* Code parts have been taken from _ped_device_probe(). */
static void probe_device(PedDevice **dev, const char *path) {
  ped_exception_fetch_all();
  *dev = ped_device_get(path);
  if (!*dev) {
    ped_exception_catch();
  }
  ped_exception_leave_all();
}

static int get_partnum(char *dev) {
  int pnum;
  char *p;

  p = dev + strlen(dev) - 1;
  while (*p && isdigit(*p) && *p != '/') {
    p--;
  }

  pnum = atoi(p + 1);
  return pnum ? pnum : 1;
}

static int get_device(char *dev) {
  PedDevice *peddev = NULL;
  int len;
  char *devname;
  char *p;
  struct stat st;

  opts.device = NULL;
  opts.fullpath = strdup(dev);

  if (stat(dev, &st) == -1) {
    return 0;
  }
  if (!(S_ISBLK(st.st_mode) && !S_ISCHR(st.st_mode))) {
    probe_device(&peddev, dev);
    if (!peddev) {
      return 0;
    }
    ped_device_destroy(peddev);
    opts.device = strdup(dev);
    return 1;
  }

  len = strlen(dev);
  p = dev + len - 1;
  while (*p && isdigit(*p) && *p != '/') {
    p--;
  }

  devname = malloc(len);
  strncpy(devname, dev, p - dev + 1);
  devname[p - dev + 1] = '\0';

  if (p-dev > 2 && devname[p-dev] == 'p' && isdigit(devname[p-dev-1])) {
    devname[p-dev] = '\0';
  }

  peddev = NULL;
  probe_device(&peddev, devname);
  if (!peddev) {
    strcpy(devname, dev);
    probe_device(&peddev, devname);

    if (!peddev) {
      free(devname);
      return 0;
    }
  } else if (opts.pnum < 0) {
    opts.pnum = get_partnum(dev);
  }
  ped_device_destroy(peddev);
  opts.device = devname;

  return 1;
}

static void resize_handler(PedTimer *timer, void *ctx) {
  int draw_this_time;
  TimerContext *tctx = (TimerContext *)ctx;

  if (opts.verbose == -1) {
    return;
  } else if (opts.verbose < 3) {
    fprintf(stdout, ".");
    fflush(stdout);
    return;
  }

  if (tctx->last_update != timer->now && timer->now > timer->start) {
    tctx->predicted_time_left = timer->predicted_end - timer->now;
    tctx->last_update = timer->now;
    draw_this_time = 1;
  } else {
    draw_this_time = 1;
  }

  if (draw_this_time) {
    printf("\r                                                            \r");
    if (timer->state_name) {
      printf("%s... ", timer->state_name);
    }
    printf("%0.f%%\t(time left %.2ld:%.2ld)", 100.0 * timer->frac,
           tctx->predicted_time_left / 60, tctx->predicted_time_left % 60);

    fflush(stdout);
  }
}

static PedExceptionOption option_get_next(PedExceptionOption options,
                                          PedExceptionOption current) {
  PedExceptionOption i;

  if (current == 0) {
    i = PED_EXCEPTION_OPTION_FIRST;
  } else {
    i = current << 1;
  }

  for (; i <= options; i <<= 1) {
    if (options & i) {
      return i;
    }
  }

  return 0;
}

static PedExceptionOption ask_for_option(PedException *ex) {
  int i;
  char *buffer;
  size_t buffer_len;
  PedExceptionOption opt;

  for (;;) {
    for (i = 0, opt = option_get_next(ex->options, 0); opt;
         i++,   opt = option_get_next(ex->options, opt)) {
      printf("%s%s", i > 0 ? "/" : "\n", ped_exception_get_option_string(opt));
    }
    printf(": ");
    fflush(stdout);

    buffer = NULL;
    buffer_len = 0;
    int line_len = getline(&buffer, &buffer_len, stdin);
    if (line_len == -1) {
      free(buffer);
      return PED_EXCEPTION_CANCEL;
    }
    buffer[line_len-1] = '\0';

    for (opt = option_get_next(ex->options, 0); opt;
         opt = option_get_next(ex->options, opt)) {
      if (!strcasecmp(buffer, ped_exception_get_option_string(opt))) {
        free(buffer);
        return opt;
      }
    }
    free(buffer);
  }

  /* Never reach there */
  return PED_EXCEPTION_CANCEL;
}

static PedExceptionOption fatresize_handler(PedException *ex) {
  PedExceptionOption opt;

  switch (ex->type) {
  case PED_EXCEPTION_INFORMATION:
  case PED_EXCEPTION_WARNING:
    fprintf(opts.force_yes ? stderr : stdout,
            "%s: %s\n", ped_exception_get_type_string(ex->type), ex->message);

    if (opts.force_yes) {
      switch (ex->options) {
        case PED_EXCEPTION_IGNORE_CANCEL:
          return PED_EXCEPTION_IGNORE;

        default:
          /* Only one choice? Take it ;-) */
          opt = option_get_next(ex->options, 0);
          if (!option_get_next(ex->options, opt)) {
            return opt;
          }
          return PED_EXCEPTION_UNHANDLED;
      }
    }

    return ask_for_option(ex);

  default:
    if (opts.verbose != -1 || isatty(0)) {
      fprintf(stderr, "%s: %s\n", ped_exception_get_type_string(ex->type),
              ex->message);
    }
    return PED_EXCEPTION_CANCEL;
  }

}

/* This function changes "sector" to "new_sector" if the new value lies
 * within the required range.
 */
static int snap(PedSector *sector, PedSector new_sector, PedGeometry *range) {
  FAT_ASSERT(ped_geometry_test_sector_inside(range, *sector), return 0);
  if (!ped_geometry_test_sector_inside(range, new_sector)) {
    return 0;
  }

  *sector = new_sector;
  return 1;
}

/* This function tries to replace the value in sector with a sequence
 * of possible replacements, given in order of preference.  The first
 * replacement that lies within the required range is adopted.
 */
static void try_snap(PedSector *sector, PedGeometry *range, ...) {
  va_list list;

  va_start(list, range);
  while (1) {
    PedSector new_sector = va_arg(list, PedSector);
    if (new_sector == -1) {
      break;
    }
    if (snap(sector, new_sector, range)) {
      break;
    }
  }
  va_end(list);
}

/* Snaps a partition to nearby partition boundaries.  This is useful for
 * gobbling up small amounts of free space, and also for reinterpreting small
 * changes to a partition as non-changes (eg: perhaps the user only wanted to
 * resize the end of a partition).
 * 	Note that this isn't the end of the story... this function is
 * always called before the constraint solver kicks in.  So you don't need to
 * worry too much about inadvertantly creating overlapping partitions, etc.
 */
static void snap_to_boundaries(PedGeometry *new_geom, PedGeometry *old_geom,
                               PedDisk *disk, PedGeometry *start_range,
                               PedGeometry *end_range) {
  PedPartition *start_part;
  PedPartition *end_part;
  PedSector start = new_geom->start;
  PedSector end = new_geom->end;

  start_part = ped_disk_get_partition_by_sector(disk, start);
  end_part = ped_disk_get_partition_by_sector(disk, end);
  FAT_ASSERT(start_part, return );
  if (!end_part) {
    return;
  }

  if (old_geom) {
    try_snap(&start, start_range, old_geom->start, start_part->geom.start,
             start_part->geom.end + 1, (PedSector)-1);
    try_snap(&end, end_range, old_geom->end, end_part->geom.end,
             end_part->geom.start - 1, (PedSector)-1);
  } else {
    try_snap(&start, start_range, start_part->geom.start,
             start_part->geom.end + 1, (PedSector)-1);
    try_snap(&end, end_range, end_part->geom.end, end_part->geom.start - 1,
             (PedSector)-1);
  }

  FAT_ASSERT(start <= end, return );
  ped_geometry_set(new_geom, start, end - start + 1);
}

/* This functions constructs a constraint from the following information:
 * 	start, is_start_exact, end, is_end_exact.
 *
 * If is_start_exact == 1, then the constraint requires start be as given in
 * "start".  Otherwise, the constraint does not set any requirements on the
 * start.
 */
static PedConstraint *constraint_from_start_end(PedDevice *dev,
                                                PedGeometry *range_start,
                                                PedGeometry *range_end) {
  return ped_constraint_new(ped_alignment_any, ped_alignment_any, range_start,
                            range_end, 1, dev->length);
}

static PedConstraint *constraint_intersect_and_destroy(PedConstraint *a,
                                                       PedConstraint *b) {
  PedConstraint *result = ped_constraint_intersect(a, b);
  ped_constraint_destroy(a);
  ped_constraint_destroy(b);
  return result;
}

static int partition_warn_busy(PedPartition *part) {
  char *path = ped_partition_get_path(part);

  if (ped_partition_is_busy(part)) {
    ped_exception_throw(PED_EXCEPTION_ERROR, PED_EXCEPTION_CANCEL,
                        ("Partition %s is being used.  You must unmount it "
                         "before you modify it with Parted."),
                        path);
    free(path);
    return 0;
  }

  free(path);
  return 1;
}

int main(int argc, char **argv) {
  int opt;

  PedDevice *dev;
  PedDisk *disk;
  PedPartition *part;
  PedTimer *timer = NULL;

  char *old_str;
  char *def_str;
  PedFileSystem *fs;
  PedSector start, end;
  PedGeometry part_geom;
  PedGeometry new_geom;
  PedGeometry *old_geom;
  PedConstraint *constraint;
  PedGeometry *range_start, *range_end;

  static const char *sopt = "-hfin:s:vpq";
  static const struct option lopt[] = {{"help", no_argument, NULL, 'h'},
                                       {"force-yes", no_argument, NULL, 'f'},
                                       {"info", no_argument, NULL, 'i'},
                                       {"partition", required_argument, NULL, 'n'},
                                       {"progress", no_argument, NULL, 'p'},
                                       {"size", required_argument, NULL, 's'},
                                       {"verbose", no_argument, NULL, 'v'},
                                       {"quiet", no_argument, NULL, 'q'},
                                       {NULL, 0, NULL, 0}};

  memset(&opts, 0, sizeof(opts));

  if (argc < 2) {
    usage(0);
  }

  opts.pnum = -1;

  while ((opt = getopt_long(argc, argv, sopt, lopt, NULL)) != -1) {
    switch (opt) {
      case 1:
        if (!opts.device) {
          get_device(optarg);
        } else {
          usage(1);
        }
        break;

      case 'n':
        opts.pnum = atoi(optarg);
        break;

      case 'f':
        opts.force_yes = 1;
        break;

      case 'i':
        opts.info = 1;
        break;

      case 'p':
        opts.progress = 1;
        break;

      case 's':
        opts.size = get_size(optarg);
        break;

      case 'v':
        opts.verbose++;
        break;

      case 'q':
        opts.verbose = -1;
        break;

      case 'h':
      case '?':
      default:
        printd(0, "%s (%s)\n", PACKAGE_STRING, BUILD_DATE);
        usage(0);
    }
  }

  printd(0, "%s (%s)\n", PACKAGE_STRING, BUILD_DATE);

  if (!opts.device) {
    fprintf(stderr, "You must specify exactly one existing device.\n");
    return 1;
  } else if (!opts.size && !opts.info) {
    fprintf(stderr, "You must specify new size.\n");
    return 1;
  }

  ped_exception_set_handler(fatresize_handler);

  if (opts.progress) {
    timer = ped_timer_new(resize_handler, &timer_context);
    timer_context.last_update = 0;
  }

  printd(3, "ped_device_get(%s)\n", opts.device);
  dev = ped_device_get(opts.device);
  if (!dev) {
    return 1;
  }

  printd(3, "ped_device_open()\n");
  if (!ped_device_open(dev)) {
    return 1;
  }

  if (opts.pnum > 0) {
    printd(3, "ped_disk_new()\n");
    disk = ped_disk_new(dev);
    if (!disk) {
      return 1;
    }

    printd(3, "ped_disk_get_partition(%d)\n", opts.pnum);
    part = ped_disk_get_partition(disk, opts.pnum);
    if (!part || !part->fs_type) {
      return 1;
    }

    if (strncmp(part->fs_type->name, "fat", 3)) {
      ped_exception_throw(PED_EXCEPTION_ERROR, PED_EXCEPTION_CANCEL,
                          "%s is not valid FAT16/FAT32 partition.",
                          opts.fullpath);
      return 1;
    }

    if (!partition_warn_busy(part)) {
      ped_disk_destroy(disk);
      return 1;
    }
    memcpy(&part_geom, &part->geom, sizeof(PedGeometry));
  } else {
    if (!ped_geometry_init(&part_geom, dev, 0, dev->length)) {
      return 1;
    }
  }

  printf("part(start=%llu, end=%llu, length=%llu)\n",
    part_geom.start, part_geom.end, part_geom.length);

  if (opts.info || opts.size == LLONG_MAX) {
    printd(3, "ped_file_system_open()\n");
    fs = ped_file_system_open(&part_geom);
    if (!fs) {
      return 1;
    }

    printd(3, "ped_file_system_get_resize_constraint()\n");
    constraint = ped_file_system_get_resize_constraint(fs);
    if (!constraint) {
      return 1;
    }
  }
  if (opts.info) {
    printf("FAT: %s\n", fs->type->name);
    printf("Cur size: %llu\n", fs->geom->length * dev->sector_size);
    printf("Min size: %llu\n", constraint->min_size * dev->sector_size);
    printf("Max size: %llu\n", constraint->max_size * dev->sector_size);

    ped_constraint_destroy(constraint);
    return 0;
  }
  if (opts.size == LLONG_MAX) {
    opts.size = constraint->max_size * dev->sector_size;
    ped_constraint_destroy(constraint);
  }

  start = part_geom.start;
  printd(3, "ped_geometry_new(%llu)\n", start);
  range_start = ped_geometry_new(dev, start, 1);
  if (!range_start) {
    return 1;
  }

  end = part_geom.start + opts.size / dev->sector_size;
  printd(3, "ped_unit_parse(%llu)\n", end);
  old_str = ped_unit_format(dev, part_geom.end);
  def_str = ped_unit_format(dev, end);
  if (!strcmp(old_str, def_str)) {
    range_end = ped_geometry_new(dev, part_geom.end, 1);
    if (!range_end) {
      return 1;
    }
  } else if (!ped_unit_parse(def_str, dev, &end, &range_end)) {
    return 1;
  }
  free(old_str);
  free(def_str);

  printd(3, "ped_geometry_duplicate()\n");
  old_geom = ped_geometry_duplicate(&part_geom);
  if (!old_geom) {
    return 1;
  }

  printd(3, "ped_geometry_init(%llu, %llu)\n", start, end - start + 1);
  if (!ped_geometry_init(&new_geom, dev, start, end - start + 1)) {
    return 1;
  }

  printd(3, "snap_to_boundaries()\n");
  snap_to_boundaries(&new_geom, &part_geom, disk, range_start, range_end);

  printd(3, "ped_file_system_open()\n");
  fs = ped_file_system_open(&part->geom);
  if (!fs) {
    return 1;
  }

  printd(3, "constraint_intersect_and_destroy()\n");
  constraint = constraint_intersect_and_destroy(
      ped_file_system_get_resize_constraint(fs),
      constraint_from_start_end(dev, range_start, range_end));
  if (!constraint) {
    return 1;
  }

  /* Resize partition */
  if (opts.pnum > 0) {
    printd(3, "ped_disk_set_partition_geom(%llu, %llu)\n", new_geom.start,
         new_geom.end);
    if (!ped_disk_set_partition_geom(disk, part, constraint, new_geom.start,
                                     new_geom.end)) {
      ped_file_system_close(fs);
      ped_constraint_destroy(constraint);
      return 1;
    }
  }

  printd(1, "Resizing file system.\n");
  if (!ped_file_system_resize(fs, &new_geom, timer)) {
    return 1;
  }
  if (opts.verbose == 3 && opts.progress) {
    printf("\n");
  }

  printd(1, "Done.\n");
  /* May have changed... fat16 -> fat32 */
  if (opts.pnum > 0) {
    ped_partition_set_system(part, fs->type);
  }
  ped_file_system_close(fs);
  ped_constraint_destroy(constraint);

  if (opts.pnum > 0) {
    printd(1, "Committing changes.\n");
    if (!ped_disk_commit(disk)) {
      return 1;
    }
    ped_disk_destroy(disk);
  }

  if (dev->boot_dirty && dev->type != PED_DEVICE_FILE) {
    ped_exception_throw(PED_EXCEPTION_WARNING, PED_EXCEPTION_OK,
                        ("You should reinstall your boot loader."
                         "Read section 4 of the Parted User "
                         "documentation for more information."));
  }

  ped_device_close(dev);

  return 0;
}
