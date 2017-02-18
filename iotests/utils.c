#define _GNU_SOURCE
#include <stdlib.h>
#include <fcntl.h>
#include <sys/time.h>
#include <linux/fs.h>
#include <sys/ioctl.h>
#include <malloc.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <syslog.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <sys/mman.h>


#include "utils.h"

extern int keepRunning;


struct timeval gettm() {
  struct timeval now;
  gettimeofday(&now, NULL);
  return now;
}

double timedouble() {
  struct timeval now = gettm();
  double tm = (now.tv_sec * 1000000 + now.tv_usec);
  return tm/1000000.0;
}


size_t blockDeviceSize(char *path) {

  int fd = open(path, O_RDONLY );
  if (fd < 0) {
    perror(path);
    return 0;
  }
  size_t file_size_in_bytes = 0;
  ioctl(fd, BLKGETSIZE64, &file_size_in_bytes);
  close(fd);
  return file_size_in_bytes;
}


void shmemWrite() {
  const int SIZE = 4096;
  const char *name = "stutools";
  char *message0= username();

  /* create the shared memory segment */
  int shm_fd = shm_open(name, O_CREAT | O_RDWR, 0666);

  /* configure the size of the shared memory segment */
  if (ftruncate(shm_fd,SIZE) != 0) {
    fprintf(stderr,"ftruncate problem\n");
  }

  /* now map the shared memory segment in the address space of the process */
  void *ptr = mmap(0,SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
  if (ptr == MAP_FAILED) {
    fprintf(stderr, "Map failed\n");
    exit(1);
  }

  sprintf(ptr,"%lf %s\n", timedouble(), message0);
  ptr += strlen(message0);
  free(message0);
}

void shmemCheck() {
  const char *name = "stutools";
  const int SIZE = 4096;

  /* open the shared memory segment */
  int shm_fd = shm_open(name, O_RDONLY, 0666);
  if (shm_fd == -1) {
    // not there
    return ;
  } else {
    fprintf(stderr,"warning: POTENTIAL multiple tests running at once\n");
  }

  /* now map the shared memory segment in the address space of the process */
  void * ptr = mmap(0,SIZE, PROT_READ, MAP_SHARED, shm_fd, 0);
  if (ptr == MAP_FAILED) {
    fprintf(stderr,"error: can't allocate shmem\n");
    // it's not there :)
    exit(1);
  }

  double time = 0;
  char s[SIZE];
  sscanf(ptr, "%lf %s", &time, s);

  fprintf(stderr,"warning: user %s, seconds ago: %.1lf %s\n", s, timedouble() - time, (timedouble() - time < 5) ? " *** NOW *** ": "");
  
}


void shmemUnlink() {
  const char *name = "stutools";
  if (shm_unlink(name) == -1) {
    printf("Error removing %s\n",name);
    exit(-1);
  }
}

void doChunks(int fd, char *label, int *chunkSizes, int numChunks, size_t maxTime, logSpeedType *l, size_t maxBufSize, size_t outputEvery, int writeAction, int sequential, int direct, int verifyWrites, float flushEverySecs) {

  // check
  //  shmemCheck();

  
  void *buf = aligned_alloc(65536, maxBufSize);
  if (!buf) { // O_DIRECT requires aligned memory
	fprintf(stderr,"memory allocation failed\n");exit(1);
  }
  srand((int)timedouble());

  char *charbuf = (char*) buf;
  size_t checksum = 0;
  const size_t startChar = rand() % 255;
  const size_t startMod = 20 + rand() % 40;
  for (size_t i = 0; i < maxBufSize; i++ ) {
    charbuf[i] = 'A' + (char) ((startChar + i ) % startMod);
    checksum += ('A' + (startChar + i) % startMod);
  }

  size_t maxblocks = 0;
  if (!sequential) {
    if (isBlockDevice(label) && ((maxblocks = blockDeviceSize(label)/4096) > 0)) {
       fprintf(stderr,"max blocks on %s is %zd\n", label, maxblocks);
    } else {
      fprintf(stderr,"error: need to be a block device with the -r option\n");
      exit(1);
    }
  } 
  int wbytes = 0;
  double lastg = timedouble();
  int chunkIndex = 0;
  double startTime = timedouble();
  double lastFdatasync = startTime;
  int resetCount = 5;
  logSpeedType previousSpeeds;

  size_t countValues = 0, allocValues = 20000, sumBytes = 0;
  double *allValues, *allTimes, *allTotal;

  allValues = malloc(allocValues * sizeof(double));
  allTimes = malloc(allocValues * sizeof(double));
  allTotal = malloc(allocValues * sizeof(double));

  logSpeedInit(&previousSpeeds);
  
  while (keepRunning) {
    //    shmemWrite(); // so other people know we're running!

    if (!sequential) {
	size_t pos = (rand() % maxblocks) * 4096;
 	//fprintf(stderr,"%zd\n", pos);	
 	off_t ret = lseek(fd, pos, SEEK_SET);
	if (ret < 0) {
	  perror("seek error");
	}
    }

    if (writeAction) {
      wbytes = write(fd, charbuf, chunkSizes[0]);
      if (wbytes < 0) {
	perror("problem writing");
	break;
      }
    } else {
      wbytes = read(fd, charbuf, chunkSizes[0]);
      if (wbytes < 0) {
	perror("problem reading");
	break;
      }
    }
    if (wbytes == 0) {
      break;
    }

    const double tt = timedouble();
    
    logSpeedAdd(l, wbytes);

    sumBytes += wbytes;
    allValues[countValues] = wbytes;
    allTimes[countValues] = tt;
    allTotal[countValues] = sumBytes;
    countValues++;
    
    if (countValues >= allocValues) {
      allocValues = allocValues * 2 + 2;
      allValues = realloc(allValues, allocValues * sizeof(double));
      allTimes = realloc(allTimes, allocValues * sizeof(double));
      allTotal = realloc(allTotal, allocValues * sizeof(double));
    }

    if ((flushEverySecs > 0) && (tt - lastFdatasync > flushEverySecs)) {
      fprintf(stderr,"fdatasync() at %.1lf seconds\n", tt - startTime);
      fdatasync(fd);
      lastFdatasync = tt;
    }
    
    if ((tt - lastg) >= outputEvery) {
      lastg = tt;
      fprintf(stderr,"%s '%s': %.1lf GiB, mean %.1f MiB/s, median %.1f MiB/s, 1%% %.1f MiB/s, 99%% %.1f MiB/s, n=%zd, %.1fs\n", writeAction ? "write" : "read", label, l->total / 1024.0 / 1024 / 1024, logSpeedMean(l) / 1024.0 / 1024, logSpeedMedian(l) / 1024.0 / 1024, logSpeedRank(l, 0.01) / 1024.0 / 1024, logSpeedRank(l, 0.99) /1024.0/1024, l->num, tt - l->starttime);
      logSpeedAdd(&previousSpeeds, logSpeedMean(l));
      if (logSpeedN(&previousSpeeds) >= 10) { // at least 10 data points before a reset
	if (keepRunning) {
	  double low = logSpeedRank(&previousSpeeds, .1);
	  double high = logSpeedRank(&previousSpeeds, .9);
	  double mean = logSpeedMean(&previousSpeeds);
	  if ((high / low > 1.05) && (mean < low || mean > high) && (resetCount > 0)) { // must be over 5% difference
	    fprintf(stderr,"  [ %.1lf < %.1lf MiB/s < %.1lf ]\n", low / 1024.0 / 1024, mean / 1024.0 / 1024, high / 1024.0 /1024);
	    fprintf(stderr,"resetting due to volatile timings (%d resets remain)\n", resetCount);
	    resetCount--;
	    logSpeedReset(l);
	    logSpeedReset(&previousSpeeds);
	    startTime = tt;
	  }
	}
      }
    }
    if (chunkIndex >= numChunks) {
      chunkIndex = 0;
    }
    if (tt - startTime > maxTime) {
      //fprintf(stderr,"timer triggered after %zd seconds\n", maxTime);
      break;
    }
  } // while loop
  double startclose = timedouble();
  if (writeAction) {
    fprintf(stderr,"flushing and closing..."); fflush(stderr);
  }
  fdatasync(fd);
  fsync(fd);
  close(fd);
  if (writeAction) {
    fprintf(stderr,"took %.1f seconds\n", timedouble() - startclose);
  }
    

  // add the very last value
  allValues[countValues] = wbytes;
  allTimes[countValues] = timedouble();
  allTotal[countValues] = sumBytes;
  countValues++;

  l->lasttime = timedouble(); // change time after closing
  if (resetCount > 0) {
    char s[1000];
    sprintf(s, "Total %s speed '%s': %.1lf GiB in %.1f s, mean %.2f MiB/s, %d bytes, %s, %s, n=%zd (stutools %s)%s\n", writeAction ? "write" : "read", label, l->total / 1024.0 / 1024 / 1024, logSpeedTime(l), logSpeedMean(l) / 1024.0 / 1024, chunkSizes[0], sequential ? "sequential" : "random", direct ? "O_DIRECT" : "pagecache", logSpeedN(l), VERSION, keepRunning ? "" : " - interrupted");
    fprintf(stderr, "%s", s);
    char *user = username();
    syslog(LOG_INFO, "%s - %s", user, s);
    free(user);
  } else {
    fprintf(stderr,"error: results too volatile. Perhaps the machine is busy?\n");
  }

  // dump all values to a log file
  char s[1000];
  sprintf(s, "log-%s", label);
  for (size_t i = 0; i < strlen(s); i++) {
    if (s[i] == '/') {
      s[i] = '_';
    }
  }
  double firsttime = 0;
  if (countValues > 0) {
    firsttime = allTimes[0];
  }
   
  FILE *fp = fopen(s, "wt"); if (!fp) {perror("problem creating logfile");exit(1);}
  fprintf(fp,"#time\tbigtime\tchunk\ttotalbytes\n");
  for (size_t i = 0; i < countValues; i++) {
    int ret = fprintf(fp,"%.6lf\t%.6lf\t%.0lf\t%.0lf\n", allTimes[i] - firsttime, allTimes[i], allValues[i], allTotal[i]);
    if (ret <= 0) {
      fprintf(stderr,"error: trouble writing log file\n");
      break;
    }
  }
  if (fflush(fp) != 0) {perror("problem flushing");}
  if (fclose(fp) != 0) {perror("problem closing");}
  free(allValues);
  free(allTimes);
  free(allTotal);

  // now verify
  if (verifyWrites) {
    checkContents(label, charbuf, chunkSizes[0], checksum, 1, l->total);
  }
   
  free(buf);


  
  //  logSpeedHistogram(&previousSpeeds);
  logSpeedFree(&previousSpeeds);
}


void writeChunks(int fd, char *label, int *chunkSizes, int numChunks, size_t maxTime, logSpeedType *l, size_t maxBufSize, size_t outputEvery, int seq, int direct, int verifyWrites, float flushEverySecs) {
  doChunks(fd, label, chunkSizes, numChunks, maxTime, l, maxBufSize, outputEvery, 1, seq, direct, verifyWrites, flushEverySecs);
}

void readChunks(int fd, char *label, int *chunkSizes, int numChunks, size_t maxTime, logSpeedType *l, size_t maxBufSize, size_t outputEvery, int seq, int direct) {
  doChunks(fd, label, chunkSizes, numChunks, maxTime, l, maxBufSize, outputEvery, 0, seq, direct, 0, 0);
}


int isBlockDevice(char *name) {
  struct stat sb;

  if (stat(name, &sb) == -1) {
    return 0;
  }
  return (S_ISBLK(sb.st_mode));
}


void dropCaches() {
  FILE *fp = fopen("/proc/sys/vm/drop_caches", "wt");
  if (fp == NULL) {
    fprintf(stderr,"error: you need sudo/root permission to drop caches\n");
    exit(1);
  }
  if (fprintf(fp, "3\n") < 1) {
    fprintf(stderr,"error: you need sudo/root permission to drop caches\n");
    exit(1);
  }
  fflush(fp);
  fclose(fp);
  fprintf(stderr,"caches dropped\n");
}


char *username() {
  char *buf = calloc(200, 1); if (buf == NULL) exit(-1);
  getlogin_r(buf, 200);
  return buf;
}


void checkContents(char *label, char *charbuf, size_t size, const size_t checksum, float checkpercentage, size_t stopatbytes) {
  fprintf(stderr,"verifying contents of '%s'...\n", label);
  int fd = open(label, O_RDONLY | O_DIRECT); // O_DIRECT to check contents
  if (fd < 0) {
    perror(label);
    exit(1);
  }

  void *rawbuf = NULL;
  if ((rawbuf = aligned_alloc(65536, size)) == NULL) { // O_DIRECT requires aligned memory
	fprintf(stderr,"memory allocation failed\n");exit(1);
  }
  size_t pos = 0;
  unsigned char *buf = (unsigned char*)rawbuf;
  unsigned long ii = (unsigned long)rawbuf;
  size_t check = 0, ok = 0, error = 0;
  srand(ii);

  keepRunning = 1;
  while (keepRunning) {
    if (pos >= stopatbytes) {
      break;
    }
    int wbytes = read(fd, buf, size);
    if (wbytes == 0) {
      break;
    }
    if (wbytes < 0) {
      perror("problem reading");
      break;
    }
    if (wbytes == size) { // only check the right size blocks
      check++;
      size_t newsum = 0;
      for (size_t i = 0; i <wbytes;i++) {
	newsum += (buf[i] & 0xff);
      }
      if (newsum != checksum) {
	error++;
	//	fprintf(stderr,"X");
	if (error < 5) {
	  fprintf(stderr,"checksum error %zd\n", pos);
	  //	  fprintf(stderr,"buffer: %s\n", buf);
	}
	if (error == 5) {
	  fprintf(stderr,"further errors not displayed\n");
	}
      } else {
	//	fprintf(stderr,".");
	ok++;
      }
      //    } else {
      //      fprintf(stderr,"eek bad aligned read\n");
    }
    pos += wbytes;
  }
  fflush(stderr);
  close(fd);

  char *user = username();
  syslog(LOG_INFO, "%s - verify '%s': %.1lf GiB, checked %zd, correct %zd, failed %zd\n", user, label, size*check/1024.0/1024/1024, check, ok, error);

  fprintf(stderr, "verify '%s': %.1lf GiB, checked %zd, correct %zd, failed %zd\n", label, size*check/1024.0/1024/1024, check, ok, error);

  if (error > 0) {
    syslog(LOG_INFO, "%s - checksum errors on '%s'\n", user, label);
    fprintf(stderr, "**CHECKSUM** errors\n");
  }
  free(user);
  free(rawbuf);
}

  