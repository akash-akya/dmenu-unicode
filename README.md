# dmenu - dynamic menu with multilanguage support
dmenu is an efficient dynamic menu for X.

This fork tries to enhance its usability with addition of multilanguage support 

Requirements
------------
In order to build dmenu you need following libraries
+ Xlib
+ Glib-2.0
+ Pango-1.0
+ Pangoxft-1.0

Installation
------------
Edit config.mk to match your local setup (dmenu is installed into
the /usr/local namespace by default).

Afterwards enter the following command to build and install dmenu
(if necessary as root):

    make clean install

Running dmenu
-------------
See the man page for details.
