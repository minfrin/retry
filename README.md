# repeat
Repeat a command until the command succeeds.

# example
```
~$ repeat --until=success false
repeat: 'false' returned 1, backing off for 10 seconds and trying again...
repeat: 'false' returned 1, backing off for 10 seconds and trying again...
repeat: 'false' returned 1, backing off for 10 seconds and trying again...
^C
```
