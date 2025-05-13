#include <unistd.h>

#include "test.h"

int main(void)
{
  struct connection conn1;
  initializeBuffers(&conn1, 5000);
  //initializeBuffers(&conn2, 5000);

  // Open a connection and check the greeting message

  connectToPort(&conn1, 10000);
  expectToRead(&conn1, "+OK Server ready (Author: Minna Kuriakose / mkuri)");
  expectNoMoreData(&conn1);

  // Check whether the ECHO command works

  writeString(&conn1, "ECHO Hello world!\r\n");
  expectToRead(&conn1, "+OK Hello world!");
  expectNoMoreData(&conn1);

  // Check whether the server rejects unknown commands

  writeString(&conn1, "BLAH\r\n");
  expectToRead(&conn1, "-ERR Unknown command");
  expectNoMoreData(&conn1);

  // Check whether the server properly handles partial writes

  writeString(&conn1, "ECH");
  expectNoMoreData(&conn1);
  sleep(1);
  writeString(&conn1, "O blah\r\nEC");
  expectToRead(&conn1, "+OK blah");
  expectNoMoreData(&conn1);
  sleep(1);
  writeString(&conn1, "HO blubb\r\nECHO xyz\r\n");
  expectToRead(&conn1, "+OK blubb");
  expectToRead(&conn1, "+OK xyz");
  expectNoMoreData(&conn1);

  // Check Multiple Clients ===
  /*connectToPort(&conn2, 10000);
  expectToRead(&conn2, "+OK Server ready (Author: Minna Kuriakose / mkuri)");
  expectNoMoreData(&conn2);
  writeString(&conn2, "ECHO Client 2!\r\n");
  expectToRead(&conn2, "+OK Client 2!");
  expectNoMoreData(&conn2);
  */

  // Check Empty Command
  //writeString(&conn1, "\r\n");
  //expectNoMoreData(&conn1);

  // Test buffer overflow
  //writeString(&conn1, "ECHO This is a very long message that should be handled properly without crashes or truncation issues\r\n");
  //expectToRead(&conn1, "+OK This is a very long message that should be handled properly without crashes or truncation issues");
  //expectNoMoreData(&conn1);

  // Check space before/after command
  //writeString(&conn1, "   ECHO Trim spaces   \r\n");
  //expectToRead(&conn1, "+OK Trim spaces");
  //expectNoMoreData(&conn1);

  // Check whether the QUIT command works
 
  writeString(&conn1, "QUIT\r\n");
  expectToRead(&conn1, "+OK Goodbye!");
  expectRemoteClose(&conn1);
  closeConnection(&conn1);


  //QUIT conn2
  /*writeString(&conn2, "QUIT\r\n");
  expectToRead(&conn2, "+OK Goodbye!");
  expectRemoteClose(&conn2);
  closeConnection(&conn2);*/

  freeBuffers(&conn1);
  //freeBuffers(&conn2);
  return 0;
}



















