/* niterm - Forked from bogl-bterm

   Copyright 2010 Kevin Lange <lange7@acm.uiuc.edu>
   
   BOGL - Ben's Own Graphics Library.
   Originally written by Edmund GRIMLEY EVANS <edmundo@rano.org>.
   Rendering design redone by Red Hat Inc, Alan Cox <alan@redhat.com>

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.
   
   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.
   
   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
   USA. */

#define _XOPEN_SOURCE 500
#include <errno.h>
#include <fcntl.h>
#include <locale.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#include <string>
extern "C" {
#include <bogl/bogl.h>
#include "bogl-bgf.h"
#include "bogl-term.h"
#include <anthy/anthy.h>
}
#include "niterm-tables.h"

// Tango color palette http://tango.freedesktop.org/Tango_Icon_Theme_Guidelines
static const unsigned char palette[16][3] = {
    {0x00, 0x00, 0x00},    /* 0: Black. */
    {0xcc, 0x00, 0x00},    /* 1: Red. */
    {0x4e, 0x9a, 0x06},    /* 2: Green. */
    {0xc4, 0xa0, 0x00},    /* 3: Brown. */
    {0x34, 0x65, 0xa4},    /* 4: Blue. */
    {0x75, 0x50, 0x7b},    /* 5: Magenta. */
    {0x06, 0x98, 0x9a},    /* 6: Cyan. */
    {0xd3, 0xd7, 0xcf},    /* 7: Light gray. */
    {0x55, 0x57, 0x53},    /* 0: Light Gray. */
    {0xef, 0x29, 0x29},    /* 1: Light Red. */
    {0x8a, 0xe2, 0x34},    /* 2: Light Green. */
    {0xfc, 0xe9, 0x4f},    /* 3: Yellow. */
    {0x72, 0x9f, 0xcf},    /* 4: Light Blue. */
    {0xad, 0x7f, 0xa8},    /* 5: Light Magenta. */
    {0x34, 0xe2, 0xe2},    /* 6: Light Cyan. */
    {0xee, 0xee, 0xec},    /* 7: White. */
};

static int child_pid = 0; // This should be a screen sesson, for various reasons
static struct termios ttysave;
static int quit = 0;

/* Out of memory.  Give up. */
static void out_of_memory (void)
{
  fprintf (stderr, "virtual memory exhausted\n");
  abort ();
}

/* Allocate AMT bytes of memory and make sure it succeeded. */
static void *xmalloc (size_t size)
{
  void *p;
  
  if (size == 0)
    return 0;
  p = malloc (size);
  if (!p)
    out_of_memory ();
  return p;
}

// XXX: FIX THIS, IT IS BROKEN
/* This first tries the modern Unix98 way of getting a pty, followed by the
 * old-fashioned BSD way in case that fails. */
int get_ptytty(int *xptyfd, int *xttyfd)
{
  char buf[16];
  int i, ptyfd, ttyfd;

  ptyfd = open("/dev/ptmx", O_RDWR);
  if (ptyfd >= 0) {
    const char *slave = ptsname(ptyfd);
    if (slave) {
      if (grantpt(ptyfd) >= 0) {
        if (unlockpt(ptyfd) >= 0) {
          ttyfd = open(slave, O_RDWR);
          if (ttyfd >= 0) {
            *xptyfd = ptyfd, *xttyfd = ttyfd;
            return 0;
          }
        }
      }
    }
    close(ptyfd);
  }

  for (i = 0; i < 32; i++) {
    sprintf(buf, "/dev/pty%c%x", "pqrs"[i/16], i%16);
    ptyfd = open(buf, O_RDWR);
    if (ptyfd < 0) {
      sprintf(buf, "/dev/pty/m%d", i);
      ptyfd = open(buf, O_RDWR);
    }
    if (ptyfd >= 0) {
      sprintf(buf, "/dev/tty%c%x", "pqrs"[i/16], i%16);
      ttyfd = open(buf, O_RDWR);
      if (ttyfd < 0) {
        sprintf(buf, "/dev/pty/s%d", i);
        ttyfd = open(buf, O_RDWR);
      }
      if (ttyfd >= 0) {
	      *xptyfd = ptyfd, *xttyfd = ttyfd;
	      return 0;
      }
      close(ptyfd);
      return 1;
    }
  }
  return 1;
}

/* Probably I should use this as a signal handler */
void send_hangup(void)
{
  if (child_pid)
    kill(child_pid, SIGHUP);
}

void sigchld(int sig)
{
  int status;
  if (waitpid(child_pid, &status, WNOHANG) > 0) {
    child_pid = 0;
    /* Reset ownership and permissions of ttyfd device? */
    tcsetattr(0, TCSAFLUSH, &ttysave);
    if (WIFEXITED (status))
      exit(WEXITSTATUS (status));
    if (WIFSIGNALED (status))
      exit(128 + WTERMSIG (status));
    if (WIFSTOPPED (status))
      exit(128 + WSTOPSIG (status));
    exit(status);
  }
  signal(SIGCHLD, sigchld);
}

void sigterm(int sig)
{
	quit = 1;
}

void spawn_shell(int ptyfd, int ttyfd, char * const *command_args)
{
  fflush(stdout);
  child_pid = fork();
  if (child_pid) {
    /* Change ownership and permissions of ttyfd device! */
    signal(SIGCHLD, sigchld);
    return;
  }

  setenv("TERM", "niterm", 1);

  close(ptyfd);

  setsid();
  ioctl(ttyfd, TIOCSCTTY, (char *)0);
  dup2(ttyfd, 0);
  dup2(ttyfd, 1);
  dup2(ttyfd, 2);
  if (ttyfd > 2)
    close(ttyfd);
  tcsetattr(0, TCSANOW, &ttysave);
  setgid(getgid());
  setuid(getuid());

  execvp(command_args[0], command_args);
  exit(127);
}

void set_window_size(int ttyfd, int x, int y)
{
  struct winsize win;

  win.ws_row = y;
  win.ws_col = x;
  win.ws_xpixel = 0;
  win.ws_xpixel = 0;
  ioctl(ttyfd, TIOCSWINSZ, &win);
}

static char *font_name;
static struct bogl_term *term;

void reload_font(int sig)
{
  struct bogl_font *font;

  font = bogl_mmap_font (font_name);
  if (font == NULL)
    {
      fprintf(stderr, "Bad font\n");
      return;
    }
  
  /* This leaks a mmap.  Since the font reloading feature is only expected
     to be used once per session (for instance, in debian-installer, after
     the font is replaced with a larger version containing more characters),
     we don't worry about the leak.  */
  free((void *)term->font);

  term->font = font;
  term->xstep = bogl_font_glyph(term->font, ' ', 0);
  term->ystep = bogl_font_height(term->font);
}

/*
 * niterm -f font.bgf [ -l locale ] [ program ]
 */

int main(int argc, char *argv[])
{
  struct termios ntio;
  int ret;
  char buf[8192];
  struct timeval tv;
  int ptyfd, ttyfd;
  struct bogl_font *font;
  char *locale, *command = NULL;
  char **command_args;
  int i;
  char o = ' ';
  int pending = 0;
  int login_shell = 0;

  font_name = (char *)"/usr/share/niterm/unifont.bgf"; // FIXME: This is stupid
  
  // XXX: Replace this with a better method
  for (i = 1 ; i < argc ; ++i) {
      int done = 0;
      if (argv[i][0] == '-')
          switch (argv[i][1])
          {
              case 'f':
              case 'l':
                  o = argv[i][1];
                  break;

              case 's':
                  login_shell = 1;
                  break;

              case '-':
                  done = 1;
                  break;

              default:
                  printf ("unknown option: %c\n", argv[i][1]);
          }
        else
            switch (o)
            {
                case ' ':
                    command = argv[i];
                    break;

                case 's':
                    login_shell = 1;
                    break;

                case 'f':
                    font_name = argv[i];
                    o = ' ';
                    break;

                case 'l':
                    locale = argv[i];
                    o = ' ';
                    break;
                // TODO: Add options for default foreground and background
            }

      if (done)
          break;
  }

  setlocale(LC_CTYPE, locale);

  if (font_name == NULL) {
    fprintf(stderr, "Usage: %s -f font.bgf [ -l locale ] [ program ]\n", argv[0]);
    return 1;
  }

  if ((font = bogl_mmap_font(font_name)) == NULL) {
    fprintf(stderr, "Bad font\n");
    return 1;
  }

  tcgetattr(0, &ttysave);

  if (!bogl_init()) {
    fprintf(stderr, "bogl: %s\n", bogl_error());
    return 1;
  }

  term = bogl_term_new(font);
  if (!term)
    exit(1);

  bogl_set_palette(0, 16, palette);

  bogl_term_redraw(term);

  if (get_ptytty(&ptyfd, &ttyfd)) {
    perror("can't get a pty");
    exit(1);
  }

  // XXX: This is probably not the best way to do this now
  //      that we're in C++...
  if (login_shell) {
    command_args = (char **)xmalloc(2 * sizeof *command_args);
    command_args[0] = (char*)"login";
    command_args[1] = NULL;
  } else if (command) {
    command_args = (char **)xmalloc(2 * sizeof *command_args);
    command_args[0] = command;
    command_args[1] = NULL;
  } else if (i < argc - 1) {
    int j;
    command_args = (char **)xmalloc((argc - i) * sizeof *command_args);
    for (j = i + 1; j < argc; ++j)
      command_args[j - (i + 1)] = argv[j];
    command_args[argc - (i + 1)] = NULL;
  } else {
    command_args = (char **)xmalloc(2 * sizeof *command_args);
    command_args[0] = (char*)"/bin/bash";
    command_args[1] = NULL;
  }
  spawn_shell(ptyfd, ttyfd, command_args);

  signal(SIGHUP, reload_font);
  signal(SIGTERM, sigterm);

  ntio = ttysave;
  ntio.c_lflag &= ~(ECHO|ISIG|ICANON|XCASE);
  ntio.c_iflag = 0;
  ntio.c_oflag &= ~OPOST;
  ntio.c_cc[VMIN] = 1;
  ntio.c_cc[VTIME] = 0;
  ntio.c_cflag |= CS8;
  ntio.c_line = 0;
  tcsetattr(0, TCSAFLUSH, &ntio);

  set_window_size(ttyfd, term->xsize, term->ysize);

  //term->def_fg = 7;
  //term->def_bg = 0;
  
  int ime_mode = 0;                       // IME mode (0 = off, 1 = input, 2 = candidate selection)
  std::string ime_buf[1024];              // IME buffer (input, segments)
  int l = 0;                              // Length of IME buffer
  int clear_screen = 0;                   // Clear from end of line on next IME write
  int ime_index = 0;                      // IME selected segment index
  std::string ime_candidates[1024][100];  // IME candidates list (per segment)
  int ime_cand_ind[1024];                 // IME candidate index (per segment)
  int ime_cand_count[1024];               // IME candidate count (per segment)

  anthy_context * ac; // Anthy context
  if (anthy_init()) { // Initialize anthy
      printf("Failed to init anthy, could not create terminal!\n");
      exit(1);
  }
  anthy_set_personality("");  // Set default `personality`
  ac = anthy_create_context(); // Initialize context
  anthy_context_set_encoding(ac, ANTHY_UTF8_ENCODING); // Set for Unicode
  
  for (;;) {
    fd_set fds;
    int max = 0;

    if (quit)
	    break;
    
    if(pending) {
    	tv.tv_sec = 0;
    	tv.tv_usec = 0;
    } else {
    	tv.tv_sec = 10;
    	tv.tv_usec = 100000;
    }
    
    FD_ZERO(&fds);
    FD_SET(0, &fds);
    FD_SET(ptyfd, &fds);
    if (ptyfd > max)
      max = ptyfd;
    ret = select(max+1, &fds, NULL, NULL, &tv);

    if (quit)
	    break;

    if (bogl_refresh) {
      /* Handle VT switching.  */
      if (bogl_refresh == 2) {
	      bogl_term_dirty (term);
	      /* This should not be necessary, but is, due to 2.6 kernel
	         bugs in the vga16fb driver.  */
	      bogl_set_palette(0, 16, palette);
	    }

      bogl_refresh = 0;
      bogl_term_redraw(term);
    }
    if (ret == 0 || (ret < 0 && errno == EINTR))
    {
      if(pending)
      {
      	pending = 0;
        bogl_term_redraw(term);
      }
      continue;
    }

    if (ret < 0)
      perror("select");
    if (FD_ISSET(0, &fds)) {
      ret = read(0, buf, sizeof(buf));
      // IME Mode:
      if (ret > 0) {
        int j = 0; // Loop through incoming characters
        for (j = 0; j < ret; j++) {
          char ch = buf[j];
          if ((int)ch == 0) { // Ctrl+Space produces the NULL character (\0)
            if (ime_mode < 1) { // Enable the IME
              ret = 0;
              ime_mode = 1; // IME is enabled
              for (int clr=0;clr<1024;clr++) // Clear the IME buffer
                ime_buf[clr] = "";
              l = 0;
              break; // Do not collect any more characters from the input
            } else { // Disable the IME
              if (l > 0) { // Commit if necessary
                for (ret = 0; ret < l + 1; ret++) { 
                  int outsize = sprintf(buf, "%s", (char *)ime_buf[ret].c_str());
                  write(ptyfd, buf, outsize);
                  ime_buf[ret] = "";
                }
                l = 0;
              }
              ret = 0; // Ensure that we don't write anything to the TTY
              ime_mode = 0; // Disable IME
              break; // Stop collecting input
            }
          } else if (ime_mode > 0 && (int)ch == 13) { // Return
            int outsize = 0;
            if (l > 0) { // Commit
              for (ret = 0; ret < l + 1; ret++) { 
                outsize = sprintf(buf, "%s", (char *)ime_buf[ret].c_str());
                write(ptyfd, buf, outsize);
                ime_buf[ret] = "";
              }
              l = 0; // Reset the IME
              for (int clr=0;clr<1024;clr++)
                ime_buf[clr] = "";
              ime_mode = 1;
              break;
            } else { // Pass through '\r'
              outsize = sprintf(buf, "%c", ch);
              write(ptyfd, buf, outsize);
            }
            ret = 0;
          } else if (ime_mode == 2) {
            //char tsbuf[1024];
            //int ssbuf = sprintf(tsbuf, ">>%i", ch);
            //bogl_term_out(term, tsbuf, ssbuf);
            if (ch == 27) {
              j++;
              ch = buf[j];
              if (ch == 91) {
                j++;
                ch = buf[j];
                if (ch == 67) {
                  ime_index++;
                  if (ime_index == l) {
                    ime_index = 0;
                  }
                } else if (ch == 68) {
                  ime_index--;
                  if (ime_index < 0) {
                    ime_index = l - 1;
                  }
                }
              }
              ret = 0;
            } else if (ch == 32) {
              // Space - set selected to current candidate
              //       - increment candidate selection
              ime_buf[ime_index] = ime_candidates[ime_index][ime_cand_ind[ime_index]];
              ime_cand_ind[ime_index]++;
              if (ime_cand_ind[ime_index] == ime_cand_count[ime_index]) {
                ime_cand_ind[ime_index] = 0;
              }
              clear_screen = 1;
              ret = 0;
            }
          } else if (ime_mode == 1) { // Read characters for IME
            if (l < 1 && (ch < 33 || ch > 126)){
              // Pass non-display characters through if the buffer is empty
              int outsize = sprintf(buf, "%c", ch);
              write(ptyfd, buf, outsize);
              ret = 0;
            } else {
              if (ch == 127) {
                // Backspace if l > 0
                if (l > 0) {
                  // Clear out blank segments
                  // XXX: This still has some bugs
                  while (ime_buf[l-1].compare("") == 0) {
                    if (l < 2) {
                      l = 0;
                      break;
                    }
                    l--;
                  }
                  // and back us up one real segment
                  if (l > 0) {
                    ime_buf[l-1] = "";
                    clear_screen = 1;
                    l--;
                  }
                }
              } else if (ch == 32) {
                // On space, switch to candidates selection
                ime_index = 0;
                ime_mode = 2;
                std::string candidate_string = "";
                for (ret = 0; ret < l + 1; ret++) {
                    candidate_string = candidate_string + ime_buf[ret];
                }
                anthy_set_string(ac, candidate_string.c_str());
                anthy_conv_stat *stats = new anthy_conv_stat();
                anthy_get_stat(ac, stats);
                for (ret = 0; ret < stats->nr_segment; ret++) {
                  char conv_buf[1024];
                  anthy_segment_stat *s_stats = new anthy_segment_stat();
                  anthy_get_segment(ac, ret, 0, conv_buf, 1024);
                  anthy_get_segment_stat(ac, ret, s_stats);
                  ime_buf[ret] = conv_buf;
                  ime_cand_ind[ret] = 0;
                  // Most likely candidate is now loaded directly
                  ime_cand_count[ret] = s_stats->nr_candidate;
                  for (int k = 0; k < ime_cand_count[ret] - 1; k++) {
                    anthy_get_segment(ac, ret, k + 1, conv_buf, 1024);
                    ime_candidates[ret][k] = conv_buf;
                  }
                  ime_candidates[ret][ime_cand_count[ret]-1] = ime_buf[ret];
                }
                l = stats->nr_segment;
                for (int clr=l;clr<1024;clr++)
                  ime_buf[clr] = "";
              } else { // Else, add character
                std::string conv_test;
                int old_l = l;   // old length
                int t_id = 0;    // conversion table index
                int changed = 0; // did we change anything?
                if (l > 0) {
                  conv_test = ime_buf[l-1] + ime_buf[l] + ch;
                    do { // Run through the three-character table
                      if (conversion_triple[t_id][0].compare(conv_test) == 0) {
                        // Found a match
                        ime_buf[l-1] = conversion_triple[t_id][1];
                        ime_buf[l] = conversion_triple[t_id][2];
                        changed = 1;
                        break;
                      }
                      t_id++;
                    } while (conversion_triple[t_id][0].compare("~~~") != 0);
                }
                t_id = 0;  // Reset table index
                old_l = l; // Reset old length
                if (changed == 0) {
                  conv_test = ime_buf[l] + ch;
                  do { // Run through the two-character and one-character table
                    if (conversion_table[t_id][0].compare(conv_test) == 0) {
                      // Found a match
                      ime_buf[l] = conversion_table[t_id][1];
                      ime_buf[l+1] = conversion_table[t_id][2];
                      l++;
                      changed = 1;
                      break;
                    }
                    t_id++;
                  } while (conversion_table[t_id][0].compare("~~~") != 0);
                }
                // If nothing was changed, push the character and advance the character count
                if (l == old_l && changed == 0) {
                  ime_buf[l+1] = ime_buf[l+1] + ch;
                  l++;
                }
              }
              ret = 0;
            }
          }
        }
      }
      if (ime_mode) {
        // If the IME is enabled...
        char tbuf[1024];
        // Save cursor, enable underline
        int sbuf = sprintf(tbuf, "%c[s%c[4m", 27, 27);
        bogl_term_out(term, tbuf, sbuf);
        // Print out the IME buffer
        for (ret = 0; ret < l + 1; ret++) {
          if (ime_mode == 2 && ime_index == ret) {
            // Reverse video for section selection
            sbuf = sprintf(tbuf, "%c[7m", 27);
            bogl_term_out(term, tbuf, sbuf);
          }
          sbuf = sprintf(tbuf, "%s", (char *)ime_buf[ret].c_str());
          bogl_term_out(term, tbuf, sbuf);
          if (ime_mode == 2 && ime_index == ret) {
            // Unreverse video
            sbuf = sprintf(tbuf, "%c[27m", 27);
            bogl_term_out(term, tbuf, sbuf);
          }
        }
        // Clear if the rest of the line if needed
        if (clear_screen) {
          sbuf = sprintf(tbuf, "%c[K", 27);
          bogl_term_out(term, tbuf, sbuf);
          clear_screen = 0;
        }
        // Disable underline and restore the cursor
        sbuf = sprintf(tbuf, "%c[24m%c[u", 27, 27);
        bogl_term_out(term, tbuf, sbuf);
        if (ime_mode == 2) {
          // Display candidates, spaced
          sbuf = sprintf(tbuf, "%c[B", 27);
          bogl_term_out(term, tbuf, sbuf);
          // Print canddiates
          for (int k = ime_cand_ind[ime_index]; k < ime_cand_ind[ime_index] + 5 && k < ime_cand_count[ime_index]; k++) {
            sbuf = sprintf(tbuf, "%s ", ime_candidates[ime_index][k].c_str());
            bogl_term_out(term, tbuf, sbuf);
          }
          sbuf = sprintf(tbuf, "%c[K%c[u", 27, 27);
          bogl_term_out(term, tbuf, sbuf);
        }
        ret = 0;
        pending = 1; // Force update
      }
      // End IME Mode
      if (ret > 0) // Write input to tty
        write(ptyfd, buf, ret);
    }
    else if (FD_ISSET(ptyfd,&fds)) {
      // Read from tty
      ret = read(ptyfd, buf, sizeof(buf));
      if (ret > 0)
      { // if data was read, push it to the terminal
        bogl_term_out(term, buf, ret);
        pending = 1;
      }
    }
  }

  return 0;
}
