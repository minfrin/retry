# retry
Retry a command until the command succeeds.

Retry captures stdin into memory as the data is passed to the repeated
command, and this captured stdin is then replayed should the command be
repeated. This makes it possible to embed the retry tool into shell pipelines.

Retry captures stdout into memory, and if the command was successful stdout
is passed on to stdout as normal, while if the command was repeated stdout is
passed to stderr instead. This ensures that output is passed to stdout once
and once only.

# downloads

The retry tool is available as RPMs through [COPR] as follows:

```
dnf copr enable minfrin/retry
dnf install retry
```

The retry tool is available by default on Debian derived systems.

# example
```
~$ retry --until=success -- false
retry: 'false' returned 1, backing off for 10 seconds and trying again...
retry: 'false' returned 1, backing off for 10 seconds and trying again...
retry: 'false' returned 1, backing off for 10 seconds and trying again...
^C
```

# more complex example
```
~$ retry -- curl --fail http://localhost/entities | \
  jq ... | \
  retry -- curl --fail -X POST http://localhost/resource | \
  logger -t resource-init
```

# staggered delays
```
~$ retry --until=success --delay "1,2,4,8,16,32,64" -- false
retry: false returned 1, backing off for 1 second and trying again...
retry: false returned 1, backing off for 2 seconds and trying again...
retry: false returned 1, backing off for 4 seconds and trying again...
retry: false returned 1, backing off for 8 seconds and trying again...
retry: false returned 1, backing off for 16 seconds and trying again...
retry: false returned 1, backing off for 32 seconds and trying again...
retry: false returned 1, backing off for 64 seconds and trying again...
retry: false returned 1, backing off for 64 seconds and trying again...
retry: false returned 1, backing off for 64 seconds and trying again...
^C
```

# fixed number of times
```
~$ retry --times=3 -- false
retry: false returned 1, backing off for 10 seconds and trying again...
retry: false returned 1, backing off for 10 seconds and trying again...
retry: false returned 1, giving up.
```

  [COPR]: <https://copr.fedorainfracloud.org/coprs/minfrin/retry/>
  [PPA]: <https://launchpad.net/~minfrin/+archive/ubuntu/retry>
