# retry
Retry a command until the command succeeds.

# example
```
~$ retry --until=success false
retry: 'false' returned 1, backing off for 10 seconds and trying again...
retry: 'false' returned 1, backing off for 10 seconds and trying again...
retry: 'false' returned 1, backing off for 10 seconds and trying again...
^C
```
