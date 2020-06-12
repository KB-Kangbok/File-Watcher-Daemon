# File-Watcher-Daemon

File watcher daemon is Linux background application that can choose several target folders to watch for any modification, backup the newest files into backup server, and restore the backup files.

## Installation

Install the application and run it using Systemd of Linux system.

```bash
make all
sudo make install
sudo systemctl start bu
sudo systemctl start fwd
```

## Configuration

#### fwd.conf

```
<target folder 1 path> <foldername to be used in backup server>
<target folder 2 path> <foldername to be used in backup server>
...

<host address for backup server>
<port number for backup server>
```

#### bu.conf

```
<port number>
<backup folder path>
```

## Restore application

```bash
sudo ./restore
```

Follow the instruction of the application after running bash code.