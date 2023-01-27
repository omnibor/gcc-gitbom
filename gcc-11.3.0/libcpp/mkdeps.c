/* Dependency generator for Makefile fragments.
   Copyright (C) 2000-2021 Free Software Foundation, Inc.
   Contributed by Zack Weinberg, Mar 2000

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 3, or (at your option) any
later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; see the file COPYING3.  If not see
<http://www.gnu.org/licenses/>.

 In other words, you are welcome to use, share and improve this program.
 You are forbidden to forbid anyone else to use, share and improve
 what you give them.   Help stamp out software-hoarding!  */

#include "config.h"
#include "system.h"
#include "mkdeps.h"
#include "internal.h"
#include <algorithm>
#include <vector>
#include "../../include/sha1.h"
#include "sha256.h"
#include <dirent.h>

#define GITOID_LENGTH_SHA1 20
#define GITOID_LENGTH_SHA256 32

/* Not set up to just include std::vector et al, here's a simple
   implementation.  */

/* Keep this structure local to this file, so clients don't find it
   easy to start making assumptions.  */
class mkdeps
{
public:
  /* T has trivial cctor & dtor.  */
  template <typename T>
  class vec
  {
  private:
    T *ary;
    unsigned num;
    unsigned alloc;

  public:
    vec ()
      : ary (NULL), num (0), alloc (0)
      {}
    ~vec ()
      {
	XDELETEVEC (ary);
      }

  public:
    unsigned size () const
    {
      return num;
    }
    const T &operator[] (unsigned ix) const
    {
      return ary[ix];
    }
    T &operator[] (unsigned ix)
    {
      return ary[ix];
    }
    void push (const T &elt)
    {
      if (num == alloc)
	{
	  alloc = alloc ? alloc * 2 : 16;
	  ary = XRESIZEVEC (T, ary, alloc);
	}
      ary[num++] = elt;
    }
  };
  struct velt
  {
    const char *str;
    size_t len;
  };

  mkdeps ()
    : module_name (NULL), cmi_name (NULL), is_header_unit (false), quote_lwm (0)
  {
  }
  ~mkdeps ()
  {
    unsigned int i;

    for (i = targets.size (); i--;)
      free (const_cast <char *> (targets[i]));
    for (i = deps.size (); i--;)
      free (const_cast <char *> (deps[i]));
    for (i = vpath.size (); i--;)
      XDELETEVEC (vpath[i].str);
    for (i = modules.size (); i--;)
      XDELETEVEC (modules[i]);
    XDELETEVEC (module_name);
    free (const_cast <char *> (cmi_name));
  }

public:
  vec<const char *> targets;
  vec<const char *> deps;
  vec<velt> vpath;
  vec<const char *> modules;

public:
  const char *module_name;
  const char *cmi_name;
  bool is_header_unit;
  unsigned short quote_lwm;
};

/* Apply Make quoting to STR, TRAIL.  Note that it's not possible to
   quote all such characters - e.g. \n, %, *, ?, [, \ (in some
   contexts), and ~ are not properly handled.  It isn't possible to
   get this right in any current version of Make.  (??? Still true?
   Old comment referred to 3.76.1.)  */

static const char *
munge (const char *str, const char *trail = nullptr)
{
  static unsigned alloc;
  static char *buf;
  unsigned dst = 0;

  for (; str; str = trail, trail = nullptr)
    {
      unsigned slashes = 0;
      char c;
      for (const char *probe = str; (c = *probe++);)
	{
	  if (alloc < dst + 4 + slashes)
	    {
	      alloc = alloc * 2 + 32;
	      buf = XRESIZEVEC (char, buf, alloc);
	    }

	  switch (c)
	    {
	    case '\\':
	      slashes++;
	      break;

	    case '$':
	      buf[dst++] = '$';
	      goto def;

	    case ' ':
	    case '\t':
	      /* GNU make uses a weird quoting scheme for white space.
		 A space or tab preceded by 2N+1 backslashes
		 represents N backslashes followed by space; a space
		 or tab preceded by 2N backslashes represents N
		 backslashes at the end of a file name; and
		 backslashes in other contexts should not be
		 doubled.  */
	      while (slashes--)
		buf[dst++] = '\\';
	      /* FALLTHROUGH  */

	    case '#':
	      buf[dst++] = '\\';
	      /* FALLTHROUGH  */

	    default:
	    def:
	      slashes = 0;
	      break;
	    }

	  buf[dst++] = c;
	}
    }

  buf[dst] = 0;
  return buf;
}

/* If T begins with any of the partial pathnames listed in d->vpathv,
   then advance T to point beyond that pathname.  */
static const char *
apply_vpath (class mkdeps *d, const char *t)
{
  if (unsigned len = d->vpath.size ())
    for (unsigned i = len; i--;)
      {
	if (!filename_ncmp (d->vpath[i].str, t, d->vpath[i].len))
	  {
	    const char *p = t + d->vpath[i].len;
	    if (!IS_DIR_SEPARATOR (*p))
	      goto not_this_one;

	    /* Do not simplify $(vpath)/../whatever.  ??? Might not
	       be necessary. */
	    if (p[1] == '.' && p[2] == '.' && IS_DIR_SEPARATOR (p[3]))
	      goto not_this_one;

	    /* found a match */
	    t = t + d->vpath[i].len + 1;
	    break;
	  }
      not_this_one:;
      }

  /* Remove leading ./ in any case.  */
  while (t[0] == '.' && IS_DIR_SEPARATOR (t[1]))
    {
      t += 2;
      /* If we removed a leading ./, then also remove any /s after the
	 first.  */
      while (IS_DIR_SEPARATOR (t[0]))
	++t;
    }

  return t;
}

/* Public routines.  */

class mkdeps *
deps_init (void)
{
  return new mkdeps ();
}

void
deps_free (class mkdeps *d)
{
  delete d;
}

/* Adds a target T.  We make a copy, so it need not be a permanent
   string.  QUOTE is true if the string should be quoted.  */
void
deps_add_target (class mkdeps *d, const char *t, int quote)
{
  t = xstrdup (apply_vpath (d, t));

  if (!quote)
    {
      /* Sometimes unquoted items are added after quoted ones.
	 Swap out the lowest quoted.  */
      if (d->quote_lwm != d->targets.size ())
	{
	  const char *lowest = d->targets[d->quote_lwm];
	  d->targets[d->quote_lwm] = t;
	  t = lowest;
	}
      d->quote_lwm++;
    }

  d->targets.push (t);
}

/* Sets the default target if none has been given already.  An empty
   string as the default target in interpreted as stdin.  The string
   is quoted for MAKE.  */
void
deps_add_default_target (class mkdeps *d, const char *tgt)
{
  /* Only if we have no targets.  */
  if (d->targets.size ())
    return;

  if (tgt[0] == '\0')
    d->targets.push (xstrdup ("-"));
  else
    {
#ifndef TARGET_OBJECT_SUFFIX
# define TARGET_OBJECT_SUFFIX ".o"
#endif
      const char *start = lbasename (tgt);
      char *o = (char *) alloca (strlen (start)
                                 + strlen (TARGET_OBJECT_SUFFIX) + 1);
      char *suffix;

      strcpy (o, start);

      suffix = strrchr (o, '.');
      if (!suffix)
        suffix = o + strlen (o);
      strcpy (suffix, TARGET_OBJECT_SUFFIX);

      deps_add_target (d, o, 1);
    }
}

void
deps_add_dep (class mkdeps *d, const char *t)
{
  gcc_assert (*t);

  t = apply_vpath (d, t);

  d->deps.push (xstrdup (t));
}

void
deps_add_vpath (class mkdeps *d, const char *vpath)
{
  const char *elem, *p;

  for (elem = vpath; *elem; elem = p)
    {
      for (p = elem; *p && *p != ':'; p++)
	continue;
      mkdeps::velt elt;
      elt.len = p - elem;
      char *str = XNEWVEC (char, elt.len + 1);
      elt.str = str;
      memcpy (str, elem, elt.len);
      str[elt.len] = '\0';
      if (*p == ':')
	p++;

      d->vpath.push (elt);
    }
}

/* Add a new module target (there can only be one).  M is the module
   name.   */

void
deps_add_module_target (struct mkdeps *d, const char *m,
			const char *cmi, bool is_header_unit)
{
  gcc_assert (!d->module_name);
  
  d->module_name = xstrdup (m);
  d->is_header_unit = is_header_unit;
  d->cmi_name = xstrdup (cmi);
}

/* Add a new module dependency.  M is the module name.  */

void
deps_add_module_dep (struct mkdeps *d, const char *m)
{
  d->modules.push (xstrdup (m));
}

/* Write NAME, with a leading space to FP, a Makefile.  Advance COL as
   appropriate, wrap at COLMAX, returning new column number.  Iff
   QUOTE apply quoting.  Append TRAIL.  */

static unsigned
make_write_name (const char *name, FILE *fp, unsigned col, unsigned colmax,
		 bool quote = true, const char *trail = NULL)
{
  if (quote)
    name = munge (name, trail);
  unsigned size = strlen (name);

  if (col)
    {
      if (colmax && col + size> colmax)
	{
	  fputs (" \\\n", fp);
	  col = 0;
	}
      col++;
      fputs (" ", fp);
    }

  col += size;
  fputs (name, fp);

  return col;
}

/* Write all the names in VEC via make_write_name.  */

static unsigned
make_write_vec (const mkdeps::vec<const char *> &vec, FILE *fp,
		unsigned col, unsigned colmax, unsigned quote_lwm = 0,
		const char *trail = NULL)
{
  for (unsigned ix = 0; ix != vec.size (); ix++)
    col = make_write_name (vec[ix], fp, col, colmax, ix >= quote_lwm, trail);
  return col;
}

/* Write the dependencies to a Makefile.  If PHONY is true, add
   .PHONY targets for all the dependencies too.  */

static void
make_write (const cpp_reader *pfile, FILE *fp, unsigned int colmax)
{
  const mkdeps *d = pfile->deps;

  unsigned column = 0;
  if (colmax && colmax < 34)
    colmax = 34;

  if (d->deps.size ())
    {
      column = make_write_vec (d->targets, fp, 0, colmax, d->quote_lwm);
      if (CPP_OPTION (pfile, deps.modules) && d->cmi_name)
	column = make_write_name (d->cmi_name, fp, column, colmax);
      fputs (":", fp);
      column++;
      make_write_vec (d->deps, fp, column, colmax);
      fputs ("\n", fp);
      if (CPP_OPTION (pfile, deps.phony_targets))
	for (unsigned i = 1; i < d->deps.size (); i++)
	  fprintf (fp, "%s:\n", munge (d->deps[i]));
    }

  if (!CPP_OPTION (pfile, deps.modules))
    return;

  if (d->modules.size ())
    {
      column = make_write_vec (d->targets, fp, 0, colmax, d->quote_lwm);
      if (d->cmi_name)
	column = make_write_name (d->cmi_name, fp, column, colmax);
      fputs (":", fp);
      column++;
      column = make_write_vec (d->modules, fp, column, colmax, 0, ".c++m");
      fputs ("\n", fp);
    }

  if (d->module_name)
    {
      if (d->cmi_name)
	{
	  /* module-name : cmi-name */
	  column = make_write_name (d->module_name, fp, 0, colmax,
				    true, ".c++m");
	  fputs (":", fp);
	  column++;
	  column = make_write_name (d->cmi_name, fp, column, colmax);
	  fputs ("\n", fp);

	  column = fprintf (fp, ".PHONY:");
	  column = make_write_name (d->module_name, fp, column, colmax,
				    true, ".c++m");
	  fputs ("\n", fp);
	}

      if (d->cmi_name && !d->is_header_unit)
	{
	  /* An order-only dependency.
	      cmi-name :| first-target
	     We can probably drop this this in favour of Make-4.3's grouped
	      targets '&:'  */
	  column = make_write_name (d->cmi_name, fp, 0, colmax);
	  fputs (":|", fp);
	  column++;
	  column = make_write_name (d->targets[0], fp, column, colmax);
	  fputs ("\n", fp);
	}
    }
  
  if (d->modules.size ())
    {
      column = fprintf (fp, "CXX_IMPORTS +=");
      make_write_vec (d->modules, fp, column, colmax, 0, ".c++m");
      fputs ("\n", fp);
    }
}

/* Open all the directories from the path specified in the result_dir
   parameter and put them in the vector pointed to by dirs parameter.
   Also create the directories which do not already exist.  */

static DIR *
open_all_directories_in_path (const char *result_dir, std::vector<DIR *> *dirs)
{
  std::string res_dir = result_dir;
  std::string path = "";
  std::string dir_name = "";
  size_t p = res_dir.find ('/');
  int dfd, absolute = 0;
  DIR *dir = NULL;

  if (p == std::string::npos)
    return NULL;
  /* If the res_dir is an absolute path.  */
  else if (p == 0)
    {
      absolute = 1;
      path += "/";
      /* Opening a root directory because an absolute path is specified.  */
      dir = opendir (path.c_str ());
      dfd = dirfd (dir);

      dirs->push_back (dir);
      res_dir.erase (0, 1);

      /* Path is of format "/<dir>" where dir does not exist. This point can be
         reached only if <dir> could not be created in the root folder, so it is
         considered as an illegal path.  */
      if ((p = res_dir.find ('/')) == std::string::npos)
        return NULL;

      /* Process sequences of adjacent occurrences of character '/'.  */
      while (p == 0)
        {
          res_dir.erase (0, 1);
          p = res_dir.find ('/');
        }

      if (p == std::string::npos)
        return NULL;
    }

  dir_name = res_dir.substr (0, p);
  path += dir_name;

  if ((dir = opendir (path.c_str ())) == NULL)
    {
      if (absolute)
        mkdirat (dfd, dir_name.c_str (), S_IRWXU);
      else
        mkdir (dir_name.c_str (), S_IRWXU);
      dir = opendir (path.c_str ());
    }

  if (dir == NULL)
    return NULL;

  dfd = dirfd (dir);

  dirs->push_back (dir);
  res_dir.erase (0, p + 1);

  while ((p = res_dir.find ('/')) != std::string::npos)
    {
      /* Process sequences of adjacent occurrences of character '/'.  */
      while (p == 0)
        {
          res_dir.erase (0, 1);
          p = res_dir.find ('/');
        }

      if (p == std::string::npos)
        break;

      dir_name = res_dir.substr (0, p);
      path += "/" + dir_name;

      if ((dir = opendir (path.c_str ())) == NULL)
        {
          mkdirat (dfd, dir_name.c_str (), S_IRWXU);
          dir = opendir (path.c_str ());
        }

      if (dir == NULL)
        return NULL;

      dfd = dirfd (dir);

      dirs->push_back (dir);
      res_dir.erase (0, p + 1);
    }

  if (res_dir.length () > 0)
    {
      path += "/" + res_dir;

      if ((dir = opendir (path.c_str ())) == NULL)
        {
          mkdirat (dfd, res_dir.c_str (), S_IRWXU);
          dir = opendir (path.c_str ());
        }

      dirs->push_back (dir);
    }

  return dir;
}

/* Close all the directories from the vector pointed to by dirs parameter.
   Should be called after calling the function open_all_directories_in_path.  */

static void
close_all_directories_in_path (std::vector<DIR *> dirs)
{
  for (unsigned i = 0; i != dirs.size(); i++)
    closedir (dirs[i]);
}

/* Calculate the SHA1 gitoid using the contents of the given file.  */

static void
calculate_sha1_omnibor (FILE* dep_file, unsigned char resblock[])
{
  fseek (dep_file, 0L, SEEK_END);
  long file_size = ftell (dep_file);
  fseek (dep_file, 0L, SEEK_SET);
  char buff_for_file_size[sizeof (long)];
  sprintf (buff_for_file_size, "%ld", file_size);

  std::string init_data = "blob " + std::to_string (file_size) + '\0';
  char *init_data_char_array = new char [init_data.length () + 1];
  strcpy (init_data_char_array, init_data.c_str ());

  char *file_contents = new char [file_size];
  fread (file_contents, 1, file_size, dep_file);

  /* Calculate the hash.  */
  struct sha1_ctx ctx;

  sha1_init_ctx (&ctx);

  sha1_process_bytes (init_data_char_array, init_data.length (), &ctx);
  sha1_process_bytes (file_contents, file_size, &ctx);

  sha1_finish_ctx (&ctx, resblock);

  delete [] file_contents;
  delete [] init_data_char_array;
}

/* Calculate the SHA1 gitoid using the given contents.  */

static void
calculate_sha1_omnibor_with_contents (std::string contents,
				      unsigned char resblock[])
{
  long file_size = contents.length ();
  char buff_for_file_size[sizeof(long)];
  sprintf (buff_for_file_size, "%ld", file_size);

  std::string init_data = "blob " + std::to_string (file_size) + '\0';
  char *init_data_char_array = new char [init_data.length () + 1];
  strcpy (init_data_char_array, init_data. c_str ());

  char *file_contents = new char [contents.length () + 1];
  strcpy (file_contents, contents.c_str ());

  /* Calculate the hash.  */
  struct sha1_ctx ctx;

  sha1_init_ctx (&ctx);

  sha1_process_bytes (init_data_char_array, init_data.length(), &ctx);
  sha1_process_bytes (file_contents, file_size, &ctx);

  sha1_finish_ctx (&ctx, resblock);

  delete [] file_contents;
  delete [] init_data_char_array;
}

/* Calculate the SHA256 gitoid using the contents of the given file.  */

static void
calculate_sha256_omnibor (FILE* dep_file, unsigned char resblock[])
{
  fseek (dep_file, 0L, SEEK_END);
  long file_size = ftell (dep_file);
  fseek (dep_file, 0L, SEEK_SET);
  char buff_for_file_size[sizeof (long)];
  sprintf (buff_for_file_size, "%ld", file_size);

  std::string init_data = "blob " + std::to_string (file_size) + '\0';
  char *init_data_char_array = new char [init_data.length () + 1];
  strcpy (init_data_char_array, init_data.c_str ());

  char *file_contents = new char [file_size];
  fread (file_contents, 1, file_size, dep_file);

  /* Calculate the hash.  */
  struct sha256_ctx ctx;

  sha256_init_ctx (&ctx);

  sha256_process_bytes (init_data_char_array, init_data.length (), &ctx);
  sha256_process_bytes (file_contents, file_size, &ctx);

  sha256_finish_ctx (&ctx, resblock);

  delete [] file_contents;
  delete [] init_data_char_array;
}

/* Calculate the SHA256 gitoid using the given contents.  */

static void
calculate_sha256_omnibor_with_contents (std::string contents,
					unsigned char resblock[])
{
  long file_size = contents.length ();
  char buff_for_file_size[sizeof(long)];
  sprintf (buff_for_file_size, "%ld", file_size);

  std::string init_data = "blob " + std::to_string (file_size) + '\0';
  char *init_data_char_array = new char [init_data.length () + 1];
  strcpy (init_data_char_array, init_data. c_str ());

  char *file_contents = new char [contents.length () + 1];
  strcpy (file_contents, contents.c_str ());

  /* Calculate the hash.  */
  struct sha256_ctx ctx;

  sha256_init_ctx (&ctx);

  sha256_process_bytes (init_data_char_array, init_data.length(), &ctx);
  sha256_process_bytes (file_contents, file_size, &ctx);

  sha256_finish_ctx (&ctx, resblock);

  delete [] file_contents;
  delete [] init_data_char_array;
}

/* Create the OmniBOR Document file using the gitoids of the dependencies and
   calculate the gitoid of that OmniBOR Document file.  Currently, supported
   hash functions are SHA1 and SHA256, so hash_size has to be either 20 (SHA1)
   or 32 (SHA256), while hash_func_type has to be either 0 (SHA1) or 1
   (SHA256).  */

static std::string
create_omnibor_document_file (std::string new_file_contents,
			      std::vector<std::string> vect_file_contents,
			      unsigned hash_size,
			      unsigned hash_func_type,
			      const char *result_dir)
{
  if ((hash_size != GITOID_LENGTH_SHA1 && hash_size != GITOID_LENGTH_SHA256) ||
      (hash_func_type != 0 && hash_func_type != 1))
    return "";

  static const char *const lut = "0123456789abcdef";
  for (unsigned ix = 0; ix != vect_file_contents.size (); ix++)
    {
      new_file_contents += "blob ";
      new_file_contents += vect_file_contents[ix];
      new_file_contents += "\n";
    }

  unsigned char resblock[hash_size];
  if (hash_func_type == 0)
    calculate_sha1_omnibor_with_contents (new_file_contents, resblock);
  else
    calculate_sha256_omnibor_with_contents (new_file_contents, resblock);

  std::string name = "";
  for (unsigned i = 0; i != hash_size; i++)
    {
      name += lut[resblock[i] >> 4];
      name += lut[resblock[i] & 15];
    }

  std::string new_file_path;
  std::string path_objects = "objects";
  DIR *dir_one = NULL;
  std::vector<DIR *> dirs;

  if (result_dir)
    {
      if ((dir_one = opendir (result_dir)) == NULL)
        {
          mkdir (result_dir, S_IRWXU);
	  dir_one = opendir (result_dir);
	}

      if (dir_one != NULL)
        {
          std::string res_dir = result_dir;
	  path_objects = res_dir + "/" + path_objects;
	  int dfd1 = dirfd (dir_one);
	  mkdirat (dfd1, "objects", S_IRWXU);
        }
      else if (strlen (result_dir) != 0)
        {
          DIR *final_dir = open_all_directories_in_path (result_dir, &dirs);
	  /* If an error occurred, illegal path is detected and the OmniBOR
	     information is not written.  */
	  /* TODO: Maybe put a message here that a specified path, in which
	     the OmniBOR information should be stored, is illegal.  */
          if (final_dir == NULL)
            {
              close_all_directories_in_path (dirs);
              return "";
            }
          else
            {
              std::string res_dir = result_dir;
	      path_objects = res_dir + "/" + path_objects;
	      int dfd1 = dirfd (final_dir);
	      mkdirat (dfd1, "objects", S_IRWXU);
            }
        }
      else
	mkdir ("objects", S_IRWXU);
    }
  /* Put the OmniBOR Document file in the current working directory.  */
  else
    mkdir ("objects", S_IRWXU);

  DIR *dir_two = opendir (path_objects.c_str ());
  if (dir_two == NULL)
    {
      close_all_directories_in_path (dirs);
      if (result_dir && dir_one)
	closedir (dir_one);
      return "";
    }

  int dfd2 = dirfd (dir_two);

  std::string path_sha = "";
  DIR *dir_three = NULL;
  if (hash_func_type == 0)
    {
      mkdirat (dfd2, "gitoid_blob_sha1", S_IRWXU);

      path_sha = path_objects + "/gitoid_blob_sha1";
      dir_three = opendir (path_sha.c_str ());
      if (dir_three == NULL)
        {
          closedir (dir_two);
          close_all_directories_in_path (dirs);
	  if (result_dir && dir_one)
	    closedir (dir_one);
          return "";
        }
    }
  else
    {
      mkdirat (dfd2, "gitoid_blob_sha256", S_IRWXU);

      path_sha = path_objects + "/gitoid_blob_sha256";
      dir_three = opendir (path_sha.c_str ());
      if (dir_three == NULL)
        {
          closedir (dir_two);
          close_all_directories_in_path (dirs);
	  if (result_dir && dir_one)
	    closedir (dir_one);
          return "";
        }
    }

  int dfd3 = dirfd (dir_three);
  mkdirat (dfd3, name.substr (0, 2).c_str (), S_IRWXU);

  std::string path_dir = path_sha + "/" + name.substr (0, 2);
  DIR *dir_four = opendir (path_dir.c_str ());
  if (dir_four == NULL)
    {
      closedir (dir_three);
      closedir (dir_two);
      close_all_directories_in_path (dirs);
      if (result_dir && dir_one)
	closedir (dir_one);
      return "";
    }

  new_file_path = path_dir + "/" + name.substr (2, std::string::npos);

  FILE *new_file = fopen (new_file_path.c_str (), "w");

  fwrite (new_file_contents.c_str (), sizeof(char), new_file_contents.length (),
	  new_file);

  fclose (new_file);
  closedir (dir_four);
  closedir (dir_three);
  closedir (dir_two);
  close_all_directories_in_path (dirs);
  if (result_dir && dir_one)
    closedir (dir_one);

  return name;
}

/* Calculate the gitoids of all the dependencies of the resulting object
   file and create the OmniBOR Document file using them.  Then calculate the
   gitoid of that file and name it with that gitoid in the format specified
   by the OmniBOR specification.  Finally, return that gitoid.  Use SHA1
   hashing algorithm for calculating all the gitoids.  */

static std::string
make_write_sha1_omnibor (const cpp_reader *pfile, const char *result_dir)
{
  static const char *const lut = "0123456789abcdef";
  std::string new_file_contents = "gitoid:blob:sha1\n";
  std::vector<std::string> vect_file_contents;
  std::string temp_file_contents;

  for (unsigned ix = 0; ix != pfile->deps->deps.size (); ix++)
    {
      FILE *dep_file = fopen (pfile->deps->deps[ix], "rb");
      unsigned char resblock[GITOID_LENGTH_SHA1];

      calculate_sha1_omnibor (dep_file, resblock);

      fclose (dep_file);

      temp_file_contents = "";

      unsigned char high, low;
      for (unsigned i = 0; i != GITOID_LENGTH_SHA1; i++)
        {
          high = resblock[i] >> 4;
          low = resblock[i] & 15;
          temp_file_contents += lut[high];
          temp_file_contents += lut[low];
        }

      vect_file_contents.push_back (temp_file_contents);
    }

  std::sort (vect_file_contents.begin (), vect_file_contents.end ());

  return create_omnibor_document_file (new_file_contents,
				       vect_file_contents,
				       GITOID_LENGTH_SHA1,
				       0,
				       result_dir);
}

/* Calculate the gitoids of all the dependencies of the resulting object
   file and create the OmniBOR Document file using them.  Then calculate the
   gitoid of that file and name it with that gitoid in the format specified
   by the OmniBOR specification.  Finally, return that gitoid.  Use SHA256
   hashing algorithm for calculating all the gitoids.  */

static std::string
make_write_sha256_omnibor (const cpp_reader *pfile, const char *result_dir)
{
  static const char *const lut = "0123456789abcdef";
  std::string new_file_contents = "gitoid:blob:sha256\n";
  std::vector<std::string> vect_file_contents;
  std::string temp_file_contents;

  for (unsigned ix = 0; ix != pfile->deps->deps.size (); ix++)
    {
      FILE *dep_file = fopen (pfile->deps->deps[ix], "rb");
      unsigned char resblock[GITOID_LENGTH_SHA256];

      calculate_sha256_omnibor (dep_file, resblock);

      fclose (dep_file);

      temp_file_contents = "";

      unsigned char high, low;
      for (unsigned i = 0; i != GITOID_LENGTH_SHA256; i++)
        {
          high = resblock[i] >> 4;
          low = resblock[i] & 15;
          temp_file_contents += lut[high];
          temp_file_contents += lut[low];
        }

      vect_file_contents.push_back (temp_file_contents);
    }

  std::sort (vect_file_contents.begin (), vect_file_contents.end ());

  return create_omnibor_document_file (new_file_contents,
				       vect_file_contents,
				       GITOID_LENGTH_SHA256,
				       1,
				       result_dir);
}

/* Write out dependencies according to the selected format (which is
   only Make at the moment).  */
/* Really we should be opening fp here.  */

void
deps_write (const cpp_reader *pfile, FILE *fp, unsigned int colmax)
{
  make_write (pfile, fp, colmax);
}

/* Calculate and write out the OmniBOR information using SHA1 hashing
   algorithm.  */

std::string
deps_write_sha1_omnibor (const cpp_reader *pfile, const char *result_dir)
{
  return make_write_sha1_omnibor (pfile, result_dir);
}

/* Calculate and write out the OmniBOR information using SHA256 hashing
   algorithm.  */

std::string
deps_write_sha256_omnibor (const cpp_reader *pfile, const char *result_dir)
{
  return make_write_sha256_omnibor (pfile, result_dir);
}

/* Write out a deps buffer to a file, in a form that can be read back
   with deps_restore.  Returns nonzero on error, in which case the
   error number will be in errno.  */

int
deps_save (class mkdeps *deps, FILE *f)
{
  unsigned int i;
  size_t size;

  /* The cppreader structure contains makefile dependences.  Write out this
     structure.  */

  /* The number of dependences.  */
  size = deps->deps.size ();
  if (fwrite (&size, sizeof (size), 1, f) != 1)
    return -1;

  /* The length of each dependence followed by the string.  */
  for (i = 0; i < deps->deps.size (); i++)
    {
      size = strlen (deps->deps[i]);
      if (fwrite (&size, sizeof (size), 1, f) != 1)
	return -1;
      if (fwrite (deps->deps[i], size, 1, f) != 1)
	return -1;
    }

  return 0;
}

/* Read back dependency information written with deps_save into
   the deps sizefer.  The third argument may be NULL, in which case
   the dependency information is just skipped, or it may be a filename,
   in which case that filename is skipped.  */

int
deps_restore (class mkdeps *deps, FILE *fd, const char *self)
{
  size_t size;
  char *buf = NULL;
  size_t buf_size = 0;

  /* Number of dependences.  */
  if (fread (&size, sizeof (size), 1, fd) != 1)
    return -1;

  /* The length of each dependence string, followed by the string.  */
  for (unsigned i = size; i--;)
    {
      /* Read in # bytes in string.  */
      if (fread (&size, sizeof (size), 1, fd) != 1)
	return -1;

      if (size >= buf_size)
	{
	  buf_size = size + 512;
	  buf = XRESIZEVEC (char, buf, buf_size);
	}
      if (fread (buf, 1, size, fd) != size)
	{
	  XDELETEVEC (buf);
	  return -1;
	}
      buf[size] = 0;

      /* Generate makefile dependencies from .pch if -nopch-deps.  */
      if (self != NULL && filename_cmp (buf, self) != 0)
        deps_add_dep (deps, buf);
    }

  XDELETEVEC (buf);
  return 0;
}
