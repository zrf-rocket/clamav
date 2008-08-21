/*
 *  Copyright (C) 2007-2008 Sourcefire, Inc.
 *
 *  Authors: Alberto Wu
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 *  MA 02110-1301, USA.
 */

#if HAVE_CONFIG_H
#include "clamav-config.h"
#endif

#include <stdio.h>
#if HAVE_STDLIB_H
#include <stdlib.h>
#endif
#if HAVE_STRING_H
#include <string.h>
#endif
#if HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#if HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif
#include <fcntl.h>
#include <zlib.h>

#include "others.h"
#include "clamav.h"
#include "scanners.h"

#define EC32(x) le32_to_host(x)
#define EC16(x) le16_to_host(x)

#ifndef O_BINARY
#define O_BINARY 0
#endif


static int real_scansis(FILE *, cli_ctx *, const char *);
static int real_scansis9x(FILE *, cli_ctx *, const char *);

/*************************************************
       This is the wrapper to the old and new
            format handlers - see below.
 *************************************************/

int cli_scansis(int desc, cli_ctx *ctx) {
  FILE *f;
  int i;
  char *tmpd;
  uint32_t uid[4];

  cli_dbgmsg("in scansis()\n");

  if (!(tmpd = cli_gentemp(NULL)))    
    return CL_ETMPDIR;
  if (mkdir(tmpd, 0700)) {
    cli_dbgmsg("SIS: Can't create temporary directory %s\n", tmpd);
    free(tmpd);
    return CL_ETMPDIR;
  }
  if (cli_leavetemps_flag)
    cli_dbgmsg("SIS: Extracting files to %s\n", tmpd);

  if ((i=dup(desc))==-1) {
    cli_dbgmsg("SIS: dup() failed\n");
    cli_rmdirs(tmpd);
    free(tmpd);
    return CL_EIO;
  }
  if (!(f=fdopen(i, "rb"))) {
    cli_dbgmsg("SIS: fdopen() failed\n");
    close(i);
    cli_rmdirs(tmpd);
    free(tmpd);
    return CL_EIO;
  }
  rewind(f);
  if (fread(uid, 16, 1, f)!=1) {
    cli_dbgmsg("SIS: unable to read UIDs\n");
    cli_rmdirs(tmpd);
    free(tmpd);
    fclose(f);
    return CL_EIO;
  }

  cli_dbgmsg("SIS: UIDS %x %x %x - %x\n", EC32(uid[0]), EC32(uid[1]), EC32(uid[2]), EC32(uid[3]));
  if (uid[2]==EC32(0x10000419))
    i=real_scansis(f, ctx, tmpd);
  else if(uid[0]==EC32(0x10201a7a)) {
    i=real_scansis9x(f, ctx, tmpd);
  }

  if (!cli_leavetemps_flag)
    cli_rmdirs(tmpd);

  free(tmpd);
  fclose(f);
  return i;
}


/*************************************************
     This is the handler for the old (pre 0.9)
                 SIS file format. 
 *************************************************/

enum {
  PKGfile,	/* I'm a real file */
  PKGlangfile,	/* Ich bin auch eine Datei */
  PKGoption,	/* options */
  PKGif,	/* #IF */
  PKGelsif,	/* #ELSIF */
  PKGelse,	/* #ELSE */
  PKGendif	/* #ENDIF */
};

enum {
  FTsimple = 0,
  FTtext,
  FTcomponent,
  FTrun,
  FTnull,
  FTmime,
  FTsubsis,
  FTcontsis,
  FTtextuninst,
  FTnotinst = 99
};

#define GETD(VAR) \
  if (sleft<4) { \
    memcpy(buff, buff+smax-sleft, sleft); \
    if ((smax=fread(buff+sleft,1,BUFSIZ-sleft,f)+sleft)<4) { \
      cli_dbgmsg("SIS: EOF\n"); \
      free(alangs); \
      return CL_CLEAN; \
    } \
    sleft=smax; \
  } \
  VAR = cli_readint32(&buff[smax-sleft]); \
  sleft-=4;


#define GETD2(VAR) {\
  if (sleft<4) { \
    memcpy(buff, buff+smax-sleft, sleft); \
    if ((smax=fread(buff+sleft,1,BUFSIZ-sleft,f)+sleft)<4) { \
      cli_dbgmsg("SIS: EOF\n"); \
      free(alangs); \
      free(ptrs); \
      return CL_CLEAN; \
    } \
    sleft=smax; \
  } \
  VAR = cli_readint32(&buff[smax-sleft]); \
  sleft-=4; \
}

#define SKIP(N) \
  if (sleft>=(N)) sleft-=(N); \
  else { \
    if ((ssize_t)((N)-sleft)<0) { \
      cli_dbgmsg("SIS: Refusing to seek back\n"); \
      free(alangs); \
      return CL_CLEAN; \
    } \
    fseek(f, (N)-sleft, SEEK_CUR); \
    sleft=smax=fread(buff,1,BUFSIZ,f); \
  }

const char *sislangs[] = {"UNKNOWN", "UK English","French", "German", "Spanish", "Italian", "Swedish", "Danish", "Norwegian", "Finnish", "American", "Swiss French", "Swiss German", "Portuguese", "Turkish", "Icelandic", "Russian", "Hungarian", "Dutch", "Belgian Flemish", "Australian English", "Belgian French", "Austrian German", "New Zealand English", "International French", "Czech", "Slovak", "Polish", "Slovenian", "Taiwanese Chinese", "Hong Kong Chinese", "PRC Chinese", "Japanese", "Thai", "Afrikaans", "Albanian", "Amharic", "Arabic", "Armenian", "Tagalog", "Belarussian", "Bengali", "Bulgarian", "Burmese", "Catalan", "Croation", "Canadian English", "International English", "South African English", "Estonian", "Farsi", "Canadian French", "Gaelic", "Georgian", "Greek", "Cyprus Greek", "Gujarati", "Hebrew", "Hindi", "Indonesian", "Irish", "Swiss Italian", "Kannada", "Kazakh", "Kmer", "Korean", "Lao", "Latvian", "Lithuanian", "Macedonian", "Malay", "Malayalam", "Marathi", "Moldovian", "Mongolian", "Norwegian Nynorsk", "Brazilian Portuguese", "Punjabi", "Romanian", "Serbian", "Sinhalese", "Somali", "International Spanish", "American Spanish", "Swahili", "Finland Swedish", "Reserved", "Tamil", "Telugu", "Tibetan", "Tigrinya", "Cyprus Turkish", "Turkmen", "Ukrainian", "Urdu", "Reserved", "Vietnamese", "Welsh", "Zulu", "Other"};
#define MAXLANG (sizeof(sislangs)/sizeof(sislangs[0]))

static char *getsistring(FILE *f, uint32_t ptr, uint32_t len) {
  char *name;
  uint32_t i;

  if (!len) return NULL;
  if (len>400) len=400;
  name = cli_malloc(len);
  if (!name) {
    cli_dbgmsg("SIS: OOM\n");
    return NULL;
  }
  fseek(f, ptr, SEEK_SET);
  if (fread(name, len, 1, f)!=1) {
    cli_dbgmsg("SIS: Unable to read string\n");
    free(name);
    return NULL;
  }
  for (i = 0 ; i < len; i+=2) name[i/2] = name[i];
  name[i/2]='\0';
  return name;
}

static int spamsisnames(FILE *f, uint16_t langs, const char **alangs) {
  uint32_t *lens, *ptrs;
  unsigned int j;

  if (!(lens = cli_malloc(sizeof(uint32_t) * langs * 2))) {
    cli_dbgmsg("SIS: OOM\n");
    return 0;
  }
  if (fread(lens, sizeof(uint32_t) * langs * 2, 1, f)!=1) {
    cli_dbgmsg("SIS: Unable to read lengths and pointers\n");
    free(lens);
    return 1;
  }
  ptrs=&lens[langs];

  for (j=0; j<langs; j++) {
    char *name = getsistring(f,EC32(ptrs[j]),EC32(lens[j]));
    if (name) {
      cli_dbgmsg("\t%s (%s - @%x, len %d)\n", name, alangs[j], EC32(ptrs[j]), EC32(lens[j]));
      free(name);
    }
  }
  free(lens);
  return 1;
}

static int real_scansis(FILE *f, cli_ctx *ctx, const char *tmpd) {
  struct {
    uint16_t filesum;
    uint16_t langs;
    uint16_t files;
    uint16_t deps;
    uint16_t ulangs;
    uint16_t instfiles;
    uint16_t drive;
    uint16_t caps;
    uint32_t version;
    uint16_t flags;
    uint16_t type;
    uint16_t verhi;
    uint16_t verlo;
    uint32_t versub;
    uint32_t plangs;
    uint32_t pfiles;
    uint32_t pdeps;
    uint32_t pcerts;
    uint32_t pnames;
    uint32_t psig;
    uint32_t pcaps;
    uint32_t uspace;
    uint32_t nspace;
  } sis;
  const char **alangs;
  uint16_t *llangs;
  unsigned int i, sleft=0, smax=0, umped=0;
  uint8_t compd, buff[BUFSIZ];

  if (fread(&sis,sizeof(sis),1,f)!=1) {
    cli_dbgmsg("SIS: Unable to read header\n");
    return CL_CLEAN;
  }
  /*  cli_dbgmsg("SIS HEADER INFO: \nFile checksum: %x\nLangs: %d\nFiles: %d\nDeps: %d\nUsed langs: %d\nInstalled files: %d\nDest drive: %d\nCapabilities: %d\nSIS Version: %d\nFlags: %x\nType: %d\nVersion: %d.%d.%d\nLangs@: %x\nFiles@: %x\nDeps@: %x\nCerts@: %x\nName@: %x\nSig@: %x\nCaps@: %x\nUspace: %d\nNspace: %d\n\n", sis.filesum, sis.langs, sis.files, sis.deps, sis.ulangs, sis.instfiles, sis.drive, sis.caps, sis.version, sis.flags, sis.type, sis.verhi, sis.verlo, sis.versub, sis.plangs, sis.pfiles, sis.pdeps, sis.pcerts, sis.pnames, sis.psig, sis.pcaps, sis.uspace, sis.nspace);
   */

#if WORDS_BIGENDIAN != 0
  sis.langs=EC16(sis.langs);
  sis.files=EC16(sis.files);
  sis.deps=EC16(sis.deps);
  sis.flags=EC16(sis.flags);
  sis.plangs=EC32(sis.plangs);
  sis.pfiles=EC32(sis.pfiles);
  sis.pdeps=EC32(sis.pdeps);
  sis.pnames=EC32(sis.pnames);
  sis.pcaps=EC32(sis.pcaps);
#endif

  if (!sis.langs || sis.langs>=MAXLANG) {
    cli_dbgmsg("SIS: Too many or too few languages found\n");
    return CL_CLEAN;
  }
  fseek(f, sis.plangs, SEEK_SET);
  if (!(llangs=cli_malloc(sis.langs * sizeof(uint16_t)))) {
    cli_dbgmsg("SIS: OOM\n");
    return CL_CLEAN;
  }
  if (fread(llangs, sis.langs * sizeof(uint16_t), 1, f)!=1) {
    cli_dbgmsg("SIS: Unable to read languages\n");
    free(llangs);
    return CL_CLEAN;
  }
  if (!(alangs=cli_malloc(sis.langs * sizeof(char *)))) {
    cli_dbgmsg("SIS: OOM\n");
    free(llangs);
    return CL_CLEAN;
  }
  for (i = 0; i< sis.langs; i++)
    alangs[i]=EC32(llangs[i])<MAXLANG ? sislangs[EC32(llangs[i])] : sislangs[0];
  free(llangs);

  if (!sis.pnames) {
    cli_dbgmsg("SIS: Application without a name?\n");
  } else {
    fseek(f, sis.pnames, SEEK_SET);
    cli_dbgmsg("SIS: Application name:\n");
    if (!spamsisnames(f, sis.langs, alangs)) {
      free(alangs);
      return CL_EMEM;
    }
  }

  if (!sis.pcaps) {
    cli_dbgmsg("SIS: Application without capabilities?\n");
  } else {
    fseek(f, sis.pcaps, SEEK_SET);
    cli_dbgmsg("SIS: Provides:\n");
    if (!spamsisnames(f, sis.langs, alangs)) {
      free(alangs);
      return CL_EMEM;
    }
  }

  if (!sis.pdeps) {
    cli_dbgmsg("SIS: No dependencies set for this application\n");
  } else {
    cli_dbgmsg("SIS: Depends on:\n");
    for (i = 0; i< sis.deps; i++) {
      struct {
	uint32_t uid;
	uint16_t verhi;
	uint16_t verlo;
	uint32_t versub;
      } dep;
      
      fseek(f, sis.pdeps + i*(sizeof(dep) + sis.langs*2*sizeof(uint32_t)), SEEK_SET);
      if (fread(&dep, sizeof(dep), 1, f)!=1) {
	cli_dbgmsg("SIS: Unable to read dependencies\n");
      } else {
	cli_dbgmsg("\tUID: %x v. %d.%d.%d\n\taka:\n", EC32(dep.uid), EC16(dep.verhi), EC16(dep.verlo), EC32(dep.versub));
	if (!spamsisnames(f, sis.langs, alangs)) {
	  free(alangs);
	  return CL_EMEM;
	}
      }
    }
  }

  compd = !(sis.flags & 0x0008);
  cli_dbgmsg("SIS: Package is%s compressed\n", (compd)?"":" not");

  fseek(f, sis.pfiles, SEEK_SET);
  for (i=0; i<sis.files; i++) {
    uint32_t pkgtype, fcount=1;
    uint32_t j;

    GETD(pkgtype);
    cli_dbgmsg("SIS: Pkgtype: %d\n", pkgtype);
    switch(pkgtype) {
    case PKGlangfile:
      fcount=sis.langs;
    case PKGfile: {
      uint32_t ftype,options,ssname,psname,sdname,pdname;
      const char *sftype;
      uint32_t *ptrs, *lens, *olens;
      char *fn;
      long fpos;

      GETD(ftype);
      GETD(options);
      GETD(ssname);
      GETD(psname);
      GETD(sdname);
      GETD(pdname);
      switch(ftype) {
      case FTsimple:
	sftype = "simple";
	break;
      case FTtext:
	sftype = "text";
	break;
      case FTcomponent:
	sftype = "component";
	break;
      case FTrun:
	sftype = "run";
	break;
      case FTnull:
	sftype = "null";
	break;
      case FTmime:
	sftype = "mime";
	break;
      case FTsubsis:
	sftype = "sub sis";
	break;
      case FTcontsis:
	sftype = "conatiner sis";
	break;
      case FTtextuninst:
	sftype = "uninstall text";
	break;
      case FTnotinst:
	sftype = "not to be intalled";
	break;
      default:
	sftype = "unknown";
      }
      cli_dbgmsg("SIS: File details:\n\tOptions: %d\n\tType: %s\n", options, sftype);
      fpos = ftell(f);
      if ((fn=getsistring(f, psname, ssname))) {
	cli_dbgmsg("\tOriginal filename: %s\n", fn);
	free(fn);
      }
      if ((fn=getsistring(f, pdname, sdname))) {
	cli_dbgmsg("\tInstalled to: %s\n", fn);
	free(fn);
      }
      fseek(f,fpos,SEEK_SET);

      if (!(ptrs=cli_malloc(fcount*sizeof(uint32_t)*3))) {
	cli_dbgmsg("\tOOM\n");
	free(alangs);
	return CL_CLEAN;
      }
      lens=&ptrs[fcount];
      olens=&ptrs[fcount*2];
      for (j=0; j<fcount; j++)
	GETD2(lens[j]);
      for (j=0; j<fcount; j++)
	GETD2(ptrs[j]);
      for (j=0; j<fcount; j++)
	GETD2(olens[j]);
      
      if (ftype!=FTnull) {
	char ofn[1024];
	int fd;

	fpos = ftell(f);

	for (j=0; j<fcount; j++) {
	  void *comp, *decomp;
	  uLongf olen;

	  if (!lens[j]) {
	    cli_dbgmsg("\tSkipping empty file\n");
	    continue;
	  }
	  if (cli_checklimits("sis", ctx,lens[j], 0, 0)!=CL_CLEAN) continue;
	  cli_dbgmsg("\tUnpacking lang#%d - ptr:%x csize:%x osize:%x\n", j, ptrs[j], lens[j], olens[j]);
	  if (!(comp=cli_malloc(lens[j]))) {
	    cli_dbgmsg("\tOOM\n");
	    free(ptrs);
	    free(alangs);
	    return CL_CLEAN;
	  }
	  fseek(f,ptrs[j],SEEK_SET);
	  if (fread(comp, lens[j], 1, f)!=1) {
	    cli_dbgmsg("\tSkipping ghost or otherwise out of archive file\n");
	    free(comp);
	    continue;
	  }
	  if (compd) {
	    if (olens[j]<=lens[j]*3 && cli_checklimits("sis", ctx, lens[j]*3, 0, 0)==CL_CLEAN)
	      olen=lens[j]*3;
	    else if (cli_checklimits("sis", ctx, olens[j], 0, 0)==CL_CLEAN)
	      olen=olens[j];
	    else {
	      free(comp);
	      continue;
	    }
	      
	    if (!(decomp=cli_malloc(olen))) {
	      cli_dbgmsg("\tOOM\n");
	      free(comp);
	      free(ptrs);
	      free(alangs);
	      return CL_CLEAN;
	    }
	    if (uncompress(decomp, &olen, comp, lens[j])!=Z_OK) {
	      cli_dbgmsg("\tUnpacking failure\n");
	      free(comp);
	      free(decomp);
	      continue;
	    }
	    free(comp);
	  } else {
	    olen = lens[j];
	    decomp = comp;
	  }
	  snprintf(ofn, 1024, "%s/sis%02d", tmpd, umped);
	  ofn[1023]='\0';
	  if ((fd=open(ofn, O_RDWR|O_CREAT|O_TRUNC|O_BINARY, 0600))==-1) {
	    cli_errmsg("SIS: unable to create output file %s - aborting.", ofn);
	    free(decomp);
	    free(ptrs);
	    free(alangs);
	    return CL_EIO;
	  }
	  if (cli_writen(fd, decomp, olen)!=(int)olen) {
	    close(fd);
	    free(decomp);
	    free(ptrs);
	    free(alangs);
	    close(fd);
	    return CL_EIO;
	  }
	  free(decomp);
	  if (cli_magic_scandesc(fd, ctx) == CL_VIRUS) {
	    close(fd);
	    free(ptrs);
	    free(alangs);
	    return CL_VIRUS;
	  }
	  close(fd);
	  umped++;
	}
	fseek(f,fpos,SEEK_SET);
      }
      free(ptrs);
      fcount=2*sizeof(uint32_t);
      break;
    }
    case PKGoption:
      cli_dbgmsg("SIS: I'm an option\n");
      GETD(fcount);
      fcount*=sis.langs*2*sizeof(uint32_t);
      break;
    case PKGif:
      cli_dbgmsg("SIS: #if\n");
      GETD(fcount);
      break;
    case PKGelsif:
      cli_dbgmsg("SIS: #elsif\n");
      GETD(fcount);
      break;
    case PKGelse:
      cli_dbgmsg("SIS: #else\n");
      fcount=0;
      break;
    case PKGendif:
      cli_dbgmsg("SIS: #endif\n");
      fcount=0;
      break;
    default:
      cli_dbgmsg("SIS: Unknown PKGtype, expect troubles\n");
      fcount=0;
    }
    SKIP(fcount);
  }

  free(alangs);
  return CL_CLEAN;
}


/*************************************************                                              
     This is the handler for the new (post 9.x) 
                  SIS file format. 
 *************************************************/

enum { T_INVALID, T_STRING, T_ARRAY, T_COMPRESSED, T_VERSION, T_VERSIONRANGE, T_DATE, T_TIME, T_DATETIME, T_UID, T_UNUSED, T_LANGUAGE, T_CONTENTS, T_CONTROLLER, T_INFO, T_SUPPORTEDLANGUAGES, T_SUPPORTEDOPTIONS, T_PREREQUISITES, T_DEPENDENCY, T_PROPERTIES, T_PROPERTY, T_SIGNATURES, T_CERTIFICATECHAIN, T_LOGO, T_FILEDESCRIPTION, T_HASH, T_IF, T_ELSEIF, T_INSTALLBLOCK, T_EXPRESSION, T_DATA, T_DATAUNIT, T_FILEDATA, T_SUPPORTEDOPTION, T_CONTROLLERCHECKSUM, T_DATACHECKSUM, T_SIGNATURE, T_BLOB, T_SIGNATUREALGORITHM, T_SIGNATURECERTIFICATECHAIN, T_DATAINDEX, T_CAPABILITIES, T_MAXVALUE };

const char *sisfields[] = {"Invalid", "String", "Array", "Compressed", "Version", "VersionRange", "Date", "Time", "DateTime", "Uid", "Unused", "Language", "Contents", "Controller", "Info", "SupportedLanguages", "SupportedOptions", "Prerequisites", "Dependency", "Properties", "Property", "Signatures", "CertificateChain", "Logo", "FileDescription", "Hash", "If", "ElseIf", "InstallBlock", "Expression", "Data", "DataUnit", "FileData", "SupportedOption", "ControllerChecksum", "DataChecksum", "Signature", "Blob", "SignatureAlgorithm", "SignatureCertificateChain", "DataIndex", "Capabilities"};

#define ALIGN4(x) (((x)&~3) + ((((x)&1)|(((x)>>1)&1))<<2))

#define HERE printf("here\n"),abort();

struct SISTREAM {
  FILE *f;
  uint8_t buff[BUFSIZ];
  uint32_t smax;
  uint32_t sleft;
  long fnext[7];
  uint32_t fsize[7];
  unsigned int level;
};

static inline int getd(struct SISTREAM *s, uint32_t *v) {
  if (s->sleft<4) {
    memcpy(s->buff, s->buff + s->smax - s->sleft, s->sleft);
    if ((s->sleft=s->smax=fread(&s->buff[s->sleft], 1, BUFSIZ - s->sleft, s->f) + s->sleft)<4) {
      return 1;
    }
  }
  *v = cli_readint32(&s->buff[s->smax - s->sleft]);
  s->sleft-=4;
  return 0;
}

static inline int getsize(struct SISTREAM *s) {
  uint32_t *fsize = &s->fsize[s->level];
  if(getd(s, fsize) || !*fsize || (*fsize)>>31 || (s->level && *fsize > s->fsize[s->level-1] * 2)) return 1;
  /* To handle crafted archives we allow the content to overflow the container but only up to 2 times the container size */
  s->fnext[s->level] = ftell(s->f) - s->sleft + *fsize;
  return 0;
}

static inline int getfield(struct SISTREAM *s, uint32_t *field) {
  int ret;
  if(!(ret = getd(s, field)))
    ret = getsize(s);
  if(!ret) {
    if (*field<T_MAXVALUE)
      cli_dbgmsg("SIS: %d:Got %s(%x) field with size %x\n", s->level, sisfields[*field], *field, s->fsize[s->level]);
    else
      cli_dbgmsg("SIS: %d:Got invalid(%x) field with size %x\n", s->level, *field, s->fsize[s->level]);
  }
  return ret;
}

static inline int skip(struct SISTREAM *s, uint32_t size) {
  long seekto;
  cli_dbgmsg("SIS: skipping %x\n", size);
  if (s->sleft>=size) s->sleft-=size;
  else {
    seekto = size - s->sleft;
    if (seekto<0) /* in case sizeof(long)==sizeof(uint32_t) */
      return 1;
    fseek(s->f, seekto, SEEK_CUR);
    /*     s->sleft = s->smax = fread(s->buff,1,BUFSIZ,s->f); */
    s->sleft = s->smax = 0;
  }
  return 0;
}

static inline int skipthis(struct SISTREAM *s) {
  return skip(s, ALIGN4(s->fsize[s->level]));
}

static inline void seeknext(struct SISTREAM *s) {
  fseek(s->f, s->fnext[s->level], SEEK_SET);
  /*   s->sleft = s->smax = fread(s->buff,1,BUFSIZ,s->f); */
  s->sleft = s->smax = 0;
}


static int real_scansis9x(FILE *f, cli_ctx *ctx, const char *tmpd) {
  struct SISTREAM stream;
  struct SISTREAM *s = &stream;
  uint32_t field, optst[]={T_CONTROLLERCHECKSUM, T_DATACHECKSUM, T_COMPRESSED};
  unsigned int i;

  s->f = f;
  s->smax = 0;
  s->sleft = 0;
  s->level = 0;

  if (getfield(s, &field) || field!=T_CONTENTS)
    return CL_CLEAN;
  s->level++;

  for (i=0;i<3;) {
    if (getfield(s, &field)) return CL_CLEAN;
    for (;i<3;i++) {
      if (field==optst[i]) {
	if (skipthis(s)) return CL_CLEAN;
	i++;
	break;
      }
    }
  }
  if (field!=T_COMPRESSED) return CL_CLEAN;

  i=0;
  while (1) { /* 1DATA */
    if (getfield(s, &field) || field!=T_DATA) break;

    s->level++; 
    while(1) { /* DATA::ARRAY */
      uint32_t atype;
      if (getfield(s, &field) || field!=T_ARRAY || getd(s, &atype) || atype!=T_DATAUNIT || s->fsize[s->level]<4) break;
      s->fsize[s->level]-=4;

      s->level++;
      while (s->fsize[s->level-1] && !getsize(s)) { /* FOREACH DATA::ARRAY::DATAUNITs */
	cli_dbgmsg("SIS: %d:Got dataunit element with size %x\n", s->level, s->fsize[s->level]);
	if (ALIGN4(s->fsize[s->level]) < s->fsize[s->level-1]) 
	  s->fsize[s->level-1]-=ALIGN4(s->fsize[s->level]);
	else
	  s->fsize[s->level-1]=0;

	s->level++;
	while(1) { /* DATA::ARRAY::DATAUNIT[x]::ARRAY */
	  if(getfield(s, &field) || field!=T_ARRAY || getd(s, &atype) || atype!=T_FILEDATA || s->fsize[s->level]<4) break;
	  s->fsize[s->level]-=4;

	  s->level++;
	  while (s->fsize[s->level-1] && !getsize(s)) { /* FOREACH DATA::ARRAY::DATAUNIT[x]::ARRAY::FILEDATA */
	    uint32_t usize, usizeh;
	    void *src, *dst;
	    char tempf[1024];
	    uLongf uusize;
	    int fd;

	    cli_dbgmsg("SIS: %d:Got filedata element with size %x\n", s->level, s->fsize[s->level]);
	    if (ALIGN4(s->fsize[s->level]) < s->fsize[s->level-1]) 
	      s->fsize[s->level-1]-=ALIGN4(s->fsize[s->level]);
	    else
	      s->fsize[s->level-1]=0;

	    s->level++;
	    while(1) { /* DATA::ARRAY::DATAUNIT[x]::ARRAY::FILEDATA[x]::COMPRESSED */
	      if(getfield(s, &field) || field!=T_COMPRESSED || getd(s, &field) || getd(s, &usize) || getd(s, &usizeh) || usizeh) break;
	      s->fsize[s->level]-=12;
	      cli_dbgmsg("SIS: File is%s compressed - size %x -> %x\n", (field)?"":" not", s->fsize[s->level], usize);
	      snprintf(tempf, 1024, "%s/sis9x%02d", tmpd, i++);
	      tempf[1023]='\0';
	      fseek(s->f, -(long)s->sleft, SEEK_CUR);
	      s->sleft = s->smax = 0;

	      if (cli_checklimits("sis", ctx,ALIGN4(s->fsize[s->level]), 0, 0)!=CL_CLEAN) break;
	      if (!(src=cli_malloc(ALIGN4(s->fsize[s->level])))) break;
	      if (fread(src, ALIGN4(s->fsize[s->level]),1,s->f) != 1) {
		free(src);
		break;
	      }

	      if(field) { /* compressed */
		int zresult;

		if (usize<=s->fsize[s->level]*3 && cli_checklimits("sis", ctx, s->fsize[s->level]*3, 0, 0)==CL_CLEAN)
		  uusize=s->fsize[s->level]*3;
		else if (cli_checklimits("sis", ctx, usize, 0, 0)==CL_CLEAN)
		  uusize=usize;
		else {
		  free(src);
		  break;
		}

		if (!(dst=cli_malloc(uusize))) {
		  free(src);
		  break;
		}
		zresult=uncompress(dst, &uusize, src, s->fsize[s->level]);
		free(src);
		if (zresult!=Z_OK) {
		  cli_dbgmsg("SIS: Inflate failure (%d)\n", zresult);
		  free(dst);
		  break;
		}
		if ((uLongf)usize != uusize)
		  cli_dbgmsg("SIS: Warning: expected size %lx but got %lx\n", (uLongf)usize, uusize);
		else
		  cli_dbgmsg("SIS: File successfully inflated\n");
	      } else { /* not compressed */
		dst = src;
		uusize = s->fsize[s->level];
	      }
	      if ((fd=open(tempf, O_RDWR|O_CREAT|O_TRUNC|O_BINARY, 0600))==-1) {
		cli_errmsg("SIS: unable to create output file %s - aborting.", tempf);
		free(dst);
		break;
	      }
	      if (cli_writen(fd, dst, uusize)!=(int)uusize) {
		free(dst);
		close(fd);
		break;
	      }
	      free(dst);
	      if (cli_magic_scandesc(fd, ctx) == CL_VIRUS) {
		close(fd);
		return CL_VIRUS;
	      }
	      close(fd);
	      break;
	    } /* DATA::ARRAY::DATAUNIT[x]::ARRAY::FILEDATA[x]::COMPRESSED */
	    s->level--;
	    seeknext(s);
	  } /* FOREACH DATA::ARRAY::DATAUNIT[x]::ARRAY::FILEDATAs */
	  s->level--;
	  break;
	} /* DATA::ARRAY::DATAUNIT[x]::ARRAY */
	s->level--;
	seeknext(s);
      } /* FOREACH DATA::ARRAY::DATAUNITs */
      s->level--;
      break;
    } /* DATA::ARRAY */
    s->level--;
    seeknext(s);
  }
  return CL_CLEAN;
}

/*************************************************
  An (incomplete) FSM approach to sis9x unpacking
    maybe needed if sisdataindex gets exploited
 *************************************************/

/* #include <stdio.h> */
/* #include <stdint.h> */
/* #include <stdlib.h> */
/* #include <string.h> */
/* #include <zlib.h> */

/*   /\* FIXME: RESEEK before spamming strings if not compressed *\/ */
/* #define SPAMSARRAY(WHO) \ */
/*   GETD(field); \ */
/*   fsz-=4; \ */
/*   if(field!=T_STRING) { \ */
/*     printf(WHO" - Unexpected array type, skipping\n"); \ */
/*     break; \ */
/*   } \ */
/*   while(fsz>4) { \ */
/*     GETD(field); \ */
/*     fsz-=4; \ */
/*     if(field && fsz<=sleft && field<=fsz) { \ */
/*       stringifycbuff(&cbuff[smax-sleft], field); \ */
/*       printf(WHO" - \"%s\"\n", &cbuff[smax-sleft]); \ */
/*     } else { \ */
/*       printf(WHO" - Name not decoded\n"); \ */
/*       break; \ */
/*     } \ */
/*     SKIP(ALIGN4(field)); \ */
/*     fsz-=(ALIGN4(field)); \ */
/*     if((int32_t)fsz<0) fsz=0; \ */
/*   } */
    

/* enum { T_INVALID, T_STRING, T_ARRAY, T_COMPRESSED, T_VERSION, T_VERSIONRANGE, T_DATE, T_TIME, T_DATETIME, T_UID, T_UNUSED, T_LANGUAGE, T_CONTENTS, T_CONTROLLER, T_INFO, T_SUPPORTEDLANGUAGES, T_SUPPORTEDOPTIONS, T_PREREQUISITES, T_DEPENDENCY, T_PROPERTIES, T_PROPERTY, T_SIGNATURES, T_CERTIFICATECHAIN, T_LOGO, T_FILEDESCRIPTION, T_HASH, T_IF, T_ELSEIF, T_INSTALLBLOCK, T_EXPRESSION, T_DATA, T_DATAUNIT, T_FILEDATA, T_SUPPORTEDOPTION, T_CONTROLLERCHECKSUM, T_DATACHECKSUM, T_SIGNATURE, T_BLOB, T_SIGNATUREALGORITHM, T_SIGNATURECERTIFICATECHAIN, T_DATAINDEX, T_CAPABILITIES, T_MAXVALUE, CUST_SKIP }; */


/* #define GETD(VAR) \ */
/*   if (cbuff) { \ */
/*     if (sleft<4) { \ */
/*       printf("Unespectedly reached end of compressed buffer\n"); \ */
/*       free(cbuff); \ */
/*       cbuff=NULL; \ */
/*       smax=sleft=0; \ */
/*     } else { \ */
/*       VAR = cli_readint32(&cbuff[smax-sleft]); \ */
/*       sleft-=4; \ */
/*     } \ */
/*   } else { \ */
/*     if (sleft<4) { \ */
/*       memcpy(buff, buff+smax-sleft, sleft); \ */
/*       if ((smax=fread(buff+sleft,1,BUFSIZ-sleft,f)+sleft)<4) { \ */
/* 	printf("EOF\n"); \ */
/* 	return -1; \ */
/*       } \ */
/*       sleft=smax; \ */
/*     } \ */
/*     VAR = cli_readint32(&buff[smax-sleft]); \ */
/*     sleft-=4; \ */
/*   } */


/* #define SKIP(N) \ */
/*   if (cbuff && sleft<=(N)) { \ */
/*     free(cbuff); \ */
/*     cbuff=NULL; \ */
/*     smax=sleft=0; \ */
/*   } \ */
/*   if (sleft>=(N)) sleft-=(N); \ */
/*   else { \ */
/*     if ((ssize_t)((N)-sleft)<0) { \ */
/*       printf("Refusing to seek back\n"); \ */
/*       return -1; \ */
/*     } \ */
/*     fseek(f, (N)-sleft, SEEK_CUR); \ */
/*     sleft=smax=fread(buff,1,BUFSIZ,f); \ */
/*   } */

/* #define RESEEK() \ */
/*   if (!cbuff) { \ */
/*     fseek(f, -(long)sleft, SEEK_CUR); \ */
/*     sleft=smax=fread(buff,1,BUFSIZ,f); \ */
/*   } */


/* #define GETSZ \ */
/*   GETD(fsz); \ */
/*   if(fsz>>31) { \ */
/*     printf("Size too big\n"); \ */
/*     return -1; \ */
/*   } */

/* #define GETSIZE(TREE) \ */
/*   GETD(fsz); \ */
/*   if(!fsz || (fsz>>31)) { \ */
/*     printf(TREE" - Wrong field size\n"); \ */
/*     goto SIS_ERROR; \ */
/*   } */


/* static void stringifycbuff(uint8_t *ptr, uint32_t len) { */
/*   uint32_t i; */

/*   if (len>400) len=400; */
/*   for(i = 0 ; i < len; i+=2) ptr[i/2] = ptr[i]; */
/*   ptr[i/2]='\0'; */
/*   return; */
/* } */

/* const char *sisfields[] = {"Invalid", "String", "Array", "Compressed", "Version", "VersionRange", "Date", "Time", "DateTime", "Uid", "Unused", "Language", "Contents", "Controller", "Info", "SupportedLanguages", "SupportedOptions", "Prerequisites", "Dependency", "Properties", "Property", "Signatures", "CertificateChain", "Logo", "FileDescription", "Hash", "If", "ElseIf", "InstallBlock", "Expression", "Data", "DataUnit", "FileData", "SupportedOption", "ControllerChecksum", "DataChecksum", "Signature", "Blob", "SignatureAlgorithm", "SignatureCertificateChain", "DataIndex", "Capabilities", "PLACEHOLDER"}; */

/* const char *sislangs[] = {"UNKNOWN", "UK English","French", "German", "Spanish", "Italian", "Swedish", "Danish", "Norwegian", "Finnish", "American", "Swiss French", "Swiss German", "Portuguese", "Turkish", "Icelandic", "Russian", "Hungarian", "Dutch", "Belgian Flemish", "Australian English", "Belgian French", "Austrian German", "New Zealand English", "International French", "Czech", "Slovak", "Polish", "Slovenian", "Taiwanese Chinese", "Hong Kong Chinese", "PRC Chinese", "Japanese", "Thai", "Afrikaans", "Albanian", "Amharic", "Arabic", "Armenian", "Tagalog", "Belarussian", "Bengali", "Bulgarian", "Burmese", "Catalan", "Croation", "Canadian English", "International English", "South African English", "Estonian", "Farsi", "Canadian French", "Gaelic", "Georgian", "Greek", "Cyprus Greek", "Gujarati", "Hebrew", "Hindi", "Indonesian", "Irish", "Swiss Italian", "Kannada", "Kazakh", "Kmer", "Korean", "Lao", "Latvian", "Lithuanian", "Macedonian", "Malay", "Malayalam", "Marathi", "Moldovian", "Mongolian", "Norwegian Nynorsk", "Brazilian Portuguese", "Punjabi", "Romanian", "Serbian", "Sinhalese", "Somali", "International Spanish", "American Spanish", "Swahili", "Finland Swedish", "Reserved", "Tamil", "Telugu", "Tibetan", "Tigrinya", "Cyprus Turkish", "Turkmen", "Ukrainian", "Urdu", "Reserved", "Vietnamese", "Welsh", "Zulu", "Other"}; */
/* #define MAXLANG (sizeof(sislangs)/sizeof(sislangs[0])) */


/* int main(int argc, char **argv) { */
/*   FILE *f = fopen(argv[1], "r"); */
/*   uint32_t uid[4]; */
/*   unsigned int i, sleft=0, smax=0, level=0; */
/*   uint8_t buff[BUFSIZ], *cbuff=NULL; */
/*   uint32_t field, fsz; */
/*   int ret = 0; */

/*   struct { */
/*     char tree[200]; */
/*     unsigned int next[200]; */
/*     unsigned int count; */
/*   } sstack; */
    
/*   const struct PIPPO { */
/*     uint32_t expect; */
/*     uint8_t optional; */
/*     uint8_t dir; */
/*     struct PIPPO *next; */
/*     char *what; */
/*   } s[] = { */
/*     { T_CONTENTS,                   0, 1, &s[1], ""}, */
/*     { T_CONTROLLERCHECKSUM,         1, 0, &s[2], ""}, */
/*     { T_DATACHECKSUM,               1, 0, &s[3], ""}, */
/*     { T_COMPRESSED,                 0, 1, &s[4], ""}, */
/*     { T_CONTROLLER,                 0, 1, &s[5], ""}, */
/*     { T_INFO,                       0, 1, &s[6], ""}, */
/*     { T_UID,                        0, 0, &s[7], "App UID"}, */
/*     { T_STRING,                     0, 0, &s[8], "Vendor name"}, */
/*     { T_ARRAY,                      0, 0, &s[9], "App names"}, */
/*     { T_ARRAY,                      0, 0, &s[10], "Vendor names"}, */
/*     { T_VERSION,                    0, 0, &s[11], "App Version"}, */
/*     { T_DATETIME,                   0, 0, &s[12], ""}, */
/*     { T_DATE,                       0, 0, &s[13], "Creation Date"}, */
/*     { T_TIME,                       0, 0, &s[14], "Creation Time"}, */
/*     { CUST_SKIP,                    4, 0, &s[15], ""}, */
/*     { T_SUPPORTEDOPTIONS,           0, 2, &s[16], ""}, */
/*     { T_SUPPORTEDLANGUAGES,         0, 1, &s[17], ""}, */
/*     { T_ARRAY,                      0, 0, &s[18], "Supported Languages"}, */
/*     { T_PREREQUISITES,              0, 2, &s[19], ""}, */
/*     { T_PROPERTIES,                 0, 0, &s[20], ""}, */
/*     { T_LOGO,                       1, 0, &s[21], ""}, */
/*     { T_INSTALLBLOCK,               0, 0, &s[22], ""}, */
/*     { T_SIGNATURECERTIFICATECHAIN,  1, 0, &s[23], ""}, */
/*     { T_DATAINDEX,                  0, 0, &s[24], ""}, */
/*     { T_DATA,                       0, 3, NULL, NULL} */
/*   }; */
/*   struct PIPPO *this; */

/*   struct { */
/*     struct PIPPO s; */
/*     uint32_t totalsize; */
/*     struct PIPPO *next; */
/*   } t_array; */
  
/*   GETD(uid[0]); */
/*   GETD(uid[1]); */
/*   GETD(uid[2]); */
/*   GETD(uid[3]); */

/*   printf("UIDS: %x %x %x - %x\n",uid[0],uid[1],uid[2],uid[3]); */

/*   sstack.next[0]=0; */
/*   sstack.count=0; */

/*   for (this=&s[0]; this; this=this->next) { */
/*     if(this->expect==CUST_SKIP) { */
/*       SKIP(this->optional); */
/*       continue; */
/*     } */
/*     if (this!=&t_array) { */
/*       GETD(field); */
/*       GETSIZE("FIXME"); */
/*     } else { */
/*       GETSIZE("FIXME"); */
/*       if (t_array.totalsize<=(ALIGN4(fsz)+4)) */
/* 	this->next = t_array.next; */
/*       t_array.totalsize-=ALIGN4(fsz)+4; */
/*       field = this->expect; */
/*     } */
/*     if(field>=T_MAXVALUE) { */
/*       printf("Bogus field found\n"); */
/*       ret=-1; */
/*       break; */
/*     } */
/*     for( ; this && this->optional && this->expect!=field; this=this->next) */
/*       printf("Skipping optional state %s\n", sisfields[this->expect]); */
/*     if(!this) { */
/*       printf("Broken SIS file\n"); */
/*       break; */
/*     } */
/*     if(!this->optional && this->expect!=field) { */
/*       printf("Error: expecing %s but found %s\n", sisfields[this->expect], sisfields[field]); */
/*       goto SIS_ERROR; */
/*     } */
/*     printf("Got %s field (%d) with size %x(%u)\n", sisfields[field], field, fsz, fsz); */

/*     switch(this->dir) { */
/*     case 1: /\* up *\/ */
/*       strncpy(&sstack.tree[sstack.next[sstack.count]], sisfields[field], 200-sstack.next[sstack.count]); */
/*       strncat(sstack.tree, ":", 200); */
/*       sstack.count++; */
/*       sstack.tree[199]='\0'; */
/*       sstack.next[sstack.count]=strlen(sstack.tree); */
/*       break; */
/*     case 0: */
/*       sstack.count--; */
/*     default: */
/*       sstack.count-=this->dir-1; */
/*       strncpy(&sstack.tree[sstack.next[sstack.count]], sisfields[field], 200-sstack.next[sstack.count]); */
/*       strncat(sstack.tree, ":", 200); */
/*       sstack.tree[199]='\0'; */
/*     } */

/*     printf("%s\n", sstack.tree); */

/*     switch(field) { */
/*     case T_CONTENTS: */
/*       continue; */
/*     case T_CONTROLLERCHECKSUM: */
/*       break; */
/*     case T_DATACHECKSUM: */
/*       break; */
/*     case T_COMPRESSED: */
/*       if(cbuff) { */
/* 	printf("Found nested compressed streams, aborting\n"); */
/* 	goto SIS_ERROR; */
/*       } else { */
/* 	uint32_t method; */
/* 	uint32_t usize; */
/* 	uint32_t misc; */
/* 	int zresult; */
/* 	uint8_t *dbuff; */
/* 	uLongf uusize; */

/* 	GETD(method); */
/* 	GETD(usize); */
/* 	GETD(misc); */
/* 	fsz-=12; */
/* 	if (misc) { */
/* 	  printf("%s filesize too big\n", sstack.tree); */
/* 	  goto SIS_ERROR; */
/* 	} */
/* 	printf("%s compression %d, size %d, usize %d\n", sstack.tree, method, fsz, usize); */
/* 	if(method) { */
/* 	  fseek(f, -(long)sleft, SEEK_CUR); */
/* 	  sleft=smax=0; */
/* 	  uusize=usize; */
/* 	  if(!(dbuff=malloc(ALIGN4(fsz)))) { */
/* 	    printf("%s Out of memory\n", sstack.tree); */
/* 	    goto SIS_ERROR; */
/* 	  } */
/* 	  if(!(cbuff=malloc(usize))) { */
/* 	    free(dbuff); */
/* 	    printf("%s Out of memory\n", sstack.tree); */
/* 	    goto SIS_ERROR; */
/* 	  } */
/* 	  if (fread(dbuff,ALIGN4(fsz),1,f) != 1) { */
/* 	    printf("%s Failed to read compressed data\n", sstack.tree); */
/* 	    free(dbuff); */
/* 	    goto SIS_ERROR; */
/* 	  } */
/* 	  zresult=uncompress(cbuff, &uusize, dbuff, fsz); */
/* 	  free(dbuff); */
/* 	  if (zresult!=Z_OK) { */
/* 	    printf("%s Unpacking failure, skipping block\n", sstack.tree); */
/* 	    goto SIS_ERROR; */
/* 	  } */
/* 	  if ((uLongf)usize != uusize) { */
/* 	    printf("%s Expected size %lx but got %lx\n", sstack.tree, (uLongf)usize, uusize); */
/* 	    goto SIS_ERROR; */
/* 	  } */
/* 	  smax=sleft=uusize; */
/* 	  fwrite(cbuff, uusize, 1, fopen("/tmp/gunz", "w")); */
/* 	} */
/* 	continue; */
/*       } */
/*     case T_CONTROLLER: */
/*       continue; */
/*     case T_INFO: */
/*       continue; */
/*     case T_UID: */
/*       GETD(field); */
/*       fsz-=4; */
/*       printf("%s %s = %x\n", sstack.tree, this->what, field); */
/*       break; */
/*     case T_STRING: */
/*       RESEEK(); */
/*       if(fsz && fsz<=sleft) { */
/* 	char *t=(cbuff)?&cbuff[smax-sleft]:&buff[smax-sleft]; */
/* 	stringifycbuff(t, fsz); */
/* 	printf("%s %s = \"%s\"\n", sstack.tree, this->what, t); */
/*       } else printf("%s %s not decoded\n", sstack.tree, this->what); */
/*       break; */
/*     case T_ARRAY: */
/*       GETD(field); */
/*       fsz-=4; */
/*       t_array.s.expect = field; */
/*       t_array.s.optional = 0; */
/*       t_array.s.dir = 0; */
/*       t_array.s.next = &t_array; */
/*       t_array.s.what = this->what; */
/*       t_array.next = this->next; */
/*       t_array.totalsize = ALIGN4(fsz); */
/*       this = &t_array; */
/*       continue; */
/*     case T_VERSION: { */
/*       uint32_t maj,min,bld; */
/*       GETD(maj); */
/*       GETD(min); */
/*       GETD(bld); */
/*       printf("%s %s = %u.%u.%u\n", sstack.tree, this->what, maj, min, bld); */
/*       fsz-=12; */
/*       break; */
/*     } */
/*     case T_DATETIME: */
/*       continue; */
/*     case T_DATE: */
/*       GETD(field); */
/*       fsz-=4; */
/*       printf("%s %s = %u-%u-%u\n", sstack.tree, this->what, field&0xffff, (field>>16)&0xff, (field>>24)&0xff); */
/*       break; */
/*     case T_TIME: */
/*       GETD(field); */
/*       fsz-=4; /\* -1 aligns to 0 *\/ */
/*       printf("%s %s = %u:%u:%u\n", sstack.tree, this->what, field&0xff, (field>>8)&0xff, (field>>16)&0xff); */
/*       break; */
/*     case T_SUPPORTEDOPTIONS: */
/*       /\* FIXME: ANYTHING USEFUL HERE? *\/ */
/*       break; */
/*     case T_SUPPORTEDLANGUAGES: */
/*       continue; */
/*     case T_LANGUAGE: */
/*       GETD(field); */
/*       fsz-=4; */
/*        printf("%s %s = \"%s\"\n", sstack.tree, this->what, field>=MAXLANG ? "Bad Language" : sislangs[field]); */
/*        break; */
/*     case T_PREREQUISITES: */
/*       break; */
/*     case T_PROPERTIES: */
/*       break; */
/*     case T_LOGO: */
/*       break; */
/*     case T_INSTALLBLOCK: */
/*       break; */
/*     case T_SIGNATURECERTIFICATECHAIN: */
/*       t_array.s.expect = field; */
/*       t_array.s.optional = 1; */
/*       t_array.s.dir = 0; */
/*       t_array.s.next = this; */
/*       this = &t_array; */
/*       break; */
/*     case T_DATAINDEX: */
/*       GETD(field); */
/*       fsz-=4; */
/*       printf("%s %s = %x\n", sstack.tree, this->what, field); */
/*       if(cbuff) { */
/* 	if(ALIGN4(fsz) || sleft) */
/* 	  printf("Trailing garbage found in compressed controller\n"); */
/* 	fsz=sleft=smax=0; */
/* 	free(cbuff); */
/* 	cbuff=NULL; */
/*       } */
/*       break; */
/*     default: */
/*       printf("Error: unhandled field %d\n", field); */
/*       goto SIS_ERROR; */
/*     } */
/*     SKIP(ALIGN4(fsz)); */
/*   } */
/*   return 0; */

/*   SIS_ERROR: */
/*   if(cbuff) free(cbuff); */
/*   fclose(f); */
/*   return 0; */
/* } */

