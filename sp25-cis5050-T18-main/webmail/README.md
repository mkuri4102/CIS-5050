After running "make", please run "./mail_server".

Note that the commands must be in strict order and strict syntax by the frontend!

It can support standard SMTP functionalities, based on the commands from either frontend server or outside email senders.

1. Note that: When we want to retrieve emails, the frontend directly interacts with master node and then backend kv_store rather than mail_server, since all emails are stored in the kv_store rather than here.
And the frontend must firstly call "GET user1 emails\r\n" (the column name must be "emails") to get all the email IDs of user1, in the format of, for example "1 2 4 6 8" (a string with IDs separated by empty spaces), and then call "GET user1 EMAIL1\r\n" or "GET user1 EMAIL4\r\n" to get a specific email in format like "Sender: xxx@penncloud|Subject: subject|Time: YYYY-MM-DD HH:MM:SS|Content: contentline1|contentline2|contentline3|".

(especially do NOT forget to convert the "|" in the email contents back to "\r\n"!)

2. Frontend server wants mail_server to send emails to one or more outside receivers.
(e.g., sender is "xxx@penncloud" and receiver is "yyy@seas.upenn.edu")
Also note that you MAY have "VPN" on to get this functionality work - relay to outside receivers.
Plus, the outside receiver's email address MUST be valid.

Commands should be sent like below (in strict order and in strict syntax and commands must be capitalized):

HELO penncloud

MAIL FROM:<xxx@penncloud>

RCPT TO:<yyy@seas.upenn.edu>

DATA

Subject: subject here (must exactly be "Subject:" at the beginning, even if it is empty)

contentline1

contentline2

. (dot to indicate end)

QUIT

3. Frontend server wants mail_server to send emails to one or more local receivers.
(e.g., sender is "xxx@penncloud" and receiver is "yyy@penncloud")
Commands are similar to above.

And in this kv storage file, it will be strictly like: 
("|" is the separator for different components of emails and is also for "end of lines" in email content)

"Sender: xxx@penncloud|Subject: subject|Time: YYYY-MM-DD HH:MM:SS|Content: contentline1|contentline2|"

Therefore, when we want to retrieve and view mails from the kv storage, we must "parse" it back.
(especially do NOT forget to convert the "|" in the email content back to "\r\n"!)

4. Outside senders send emails to local receivers. (e.g., use Thunderbird)
(e.g., sender is "xxx@zzz" and receiver is "yyy@penncloud")
This can be tested using telnet or Thunderbird on the same host machine.

5. Local user "replies" to an email (previously received) to the original sender (local or outside).
(Note that the emailID should be of the original email)

Commands must be like "REPL username emailID", (e.g., "REPL jack 4\r\n").
(Note that the username is the person who replies, NOT the original sender.)

then on a new line, send a command "DATA",
and then on a new line, send the contents (contents can have multiple lines),
and then on a new line, send a dot "." to denote end. (somehow similar to sending a new email)

Finally, send "QUIT\r\n".
Also, do NOT send a new subject because the subject is by default "RE: old_subject".
You only send the message you want to reply with, WITHOUT a new subject.

6. Local user "forwards" an email (previously received) to one or more receiver (local or outside).
(Note that the emailID should be of the original email, like "5" or "12".)

Command must be like "FORW username emailID receiverEmail\r\n",
or if with multiple receivers "FORW username emailID receiverEmail1 receiverEmail2 receiverEmail3\r\n".
And the new subject is by default "FWD: old_subject".
(e.g., "FORW jack 4 alice@penncloud mike@seas.upenn.edu\r\n")

7. Local user "deletes" an email (previously received),
send command "DELE username ID\r\n". (ID is the identifier of that email)
(e.g., "DELE jack 3\r\n")
