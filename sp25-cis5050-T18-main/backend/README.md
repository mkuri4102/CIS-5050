DO NOT modify config.txt.
MAKE SURE the commands are syntactically correct and follow the order.
Every normal command MUST end with "\r\n", EXCEPT sending "real contents" (do NOT add extra "\r\n" for that).

FOR Master Node: ("127.0.0.1:5050")
Run "./master config.txt"

Commands for Master:

[Frontend MUST ask Master first, then redirect to tablet node accordingly.]

0. After connection, it returns "+OK Master ready\r\n". Make sure you receive it, then you can send the following commands.

1. Lookup a row_key, send "ASK row_key\r\n", it returns "+OK REDIRECT IP:Port\r\n".
[That one is always the "current available one" for the row_key.] 
[If all dead for the group, it returns ""-ERR ALL DEAD\r\n"". Then, the user should know "Service Unavailable Now".]

2. For Admin Console, send "LIST_NODES\r\n", it returns in the exact format (for example) ["-1" means None]:
"+OK Shard0: Primary: 1 Alive: 1 2 Dead: 0 Shard1: Primary: -1 Alive: -1 Dead: 3 4 5 Shard2: Primary: 6 Alive: 6 7 8 Dead: -1\r\n".
[Please parse it carefully. The number is the node index. And "-1" means "None".]

3. Only for other tablets' usage: "ASK_PRIMARY shard_i\r\n" (shard_i is 0, 1, 2 in our case), it returns "+OK primary_index\r\n".
[If all died (no primary) for that shard, it returns "+OK -1\r\n".]

4. "QUIT\r\n" will close the connection for the client.


FOR Tablet Node:
Run "./tablet config.txt node_index" (from 0 to 8 in our case)

There will be 18 files on disk permanently (1 checkpoint file + 1 log file for 9 nodes).
But each node can only get access to its own files.
Each node is a separate process and can ONLY communicate via network.

Commands for Tablet:

0. After connection, it returns "+OK Connected\r\n". Make sure you receive it, then you can send your command.

1. "GET row col\r\n" will either return "-ERR Not found\r\n";
or first return "+OK size\r\n" (like "+OK 39546732\r\n"), after that you MUST send "READY\r\n" to backend,
and then it will return the value (with loop to send).
[Make sure you loop until recv all the bytes.]

2. "PUT r c size\r\n", (like "PUT john paper1 23245235\r\n"), it returns "+OK\r\n" to you, 
and after you receive "+OK\r\n" from backend, you can send all the bytes (perhaps with loop),
and the backend will keep receiving until collecting all bytes. After that, backend will send "+OK All bytes received\r\n".
[Make sure you can receive this message after sending all bytes.]

3. "CPUT r c old_val new_val\r\n", it returns either "+OK CPUT Success\r\n",
or "-ERR CPUT Failure\r\n" (if old_val does not match or if r,c does not exist).

4. "DELETE r c\r\n", it returns either "+OK Deleted\r\n" or "-ERR Not found\r\n" if r, c does not exist.

5. "GET_ROWS\r\n", it returns "+OK row1 row2 row3\r\n".
[If there are NO rows for now in this tablet, it just returns "+OK\r\n".]

6. "GET_COLS row\r\n", it returns either "+OK col1 col2 col3\r\n" (all col names under this row separated by " "),
or it returns "-ERR Not found\r\n" if there is NO such row.

7. Only for recovering nodes, "CHECKPOINT_VERSION subtablet\r\n", it returns "versionNumber\r\n", and then expect that node to send either "NO_NEED\r\n" or "WANT\r\n". If "NO_NEED\r\n", it will not respond. If "WANT\r\n", it will return "bytes\r\n" to show how many bytes are coming, and then expect "READY\r\n" from that node, and then send all the bytes in the chk file (including versionNumber).

8. Only for recovering nodes, "LOG_NUM subtablet\r\n", it returns "logCount\r\n", and then expect that node to either send "NO_NEED\r\n" or it will ask for last N (N > 0) entries that node was missing ("missingCount\r\n"), and it will return "bytes\r\n" to show how many bytes are coming, and then expect "READY\r\n" from that node, and then send all the bytes (ONLY the contents of last N entries).

9. Only for recovering nodes, "CUR_TAB\r\n" will return the "index" of current tablet in memory ending with "\r\n".

10. Only for primary-to-secondary, "LOAD tablet\r\n" will return "+OK\r\n". (chk&evict current tablet and load that)

11. Only for Admin Console, sending "KILL\r\n" will make this node fake dead.

12. Only for Admin Console, sending "RESTART\r\n" will restart this node (and it will recover the state).

13. "QUIT\r\n" will close the connection for the client.
