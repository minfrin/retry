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

There is also a [PPA] for Ubuntu as follows:

```
sudo add-apt-repository ppa:minfrin/retry
sudo apt-get update
sudo apt-get install retry
```

# example
```
~$ retry --until=success false
retry: 'false' returned 1, backing off for 10 seconds and trying again...
retry: 'false' returned 1, backing off for 10 seconds and trying again...
retry: 'false' returned 1, backing off for 10 seconds and trying again...
^C
```

# more complex example
```
~$ retry curl --fail http://localhost/entities | \
  jq ... | \
  retry curl --fail -X POST http://localhost/resource | \
  logger -t resource-init
```

  [COPR]: <https://copr.fedorainfracloud.org/coprs/minfrin/retry/>
  [PPA]: <https://launchpad.net/~minfrin/+archive/ubuntu/retry>
