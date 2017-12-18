# thunar-nextcloud-plugin
A plugin for sharing files via the Nextcloud client from within the Thunar file manager

## What/why/how is this?
I use Nextcloud extensively and I also use the XFCE desktop with the Thunar file manager. However, every time I wanted to share a folder/file with some other people, I had to log into the web interface. I wanted to be able to share files from within Thunar directly. So I wrote this plugin.

## INSTALLING
Run the script `compile.sh`. It should generate a file named `thunar-nextcloud-plugin.so`. As root, copy that file into `/usr/lib/x86_64-linux-gnu/thunarx-2/` (assuming you're running an amd64 Linux). Restart thunar (logging out and back in might be easiest if you don't know how to kill the background process).

## KNOWN BUGS
In some cases, the internal state of the plugin might be inconsistent. This can happen if you remove a directory that was synced via Nextcloud without removing it from the client first.

## CREDITS
This plugin was heavily inspired (read: copied) from the thunar-archive-plugin. I had no prior experience in writing Thunar plugins, so I took an existing plugin and rewrote what I needed.
I have no affiliation to the Nextcloud development team and they can probably not help you with problems related to this plugin. Also, I cannot help you with problems related to your Nextcloud installation/client.

## HACKING
Please feel free to contribute (i.e. send pull requests). There are a lot of bugs, unnecessary lines of code and missing functionality/documentation. I don't know if I have the time to fix everything myself, but I'm happy to review and merge any improvements.
