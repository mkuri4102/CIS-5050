# sp25-cis5050-T18
Please run this project on macOS environment and use Google Chrome. 
To send emails to outside receivers (e.g., sender is "xxx@penncloud" and receiver is "yyy@seas.upenn.edu"), please ensure you are connected to Penn VPN (GlobalConnect) and that the outside receiver's email address is  valid. 

**Please follow these instruction in order to run the application:**
1. Run backend: Open a terminal and navigate to the sp25-cis5050-T18/backend folder. Run "make" on the folder and then run the shell script: ./start_tablets.sh.
2. Alternatively, to run each backend server individually to test for fault tolerance,  run ./master config.txt, ./tablet config.txt 0, ./tablet config.txt 1, ./tablet config.txt 2, ./tablet config.txt 3, ./tablet config.txt 4, ./tablet config.txt 5, ./tablet config.txt 6, ./tablet config.txt 7, and ./tablet config.txt 8 on separate terminals windows
3. Run mail server: Open a new terminal and navigate to sp25-cis5050-T18/webmail folder. Run "make" on the folder and then run: ./mail_server
4. Run frontend: Open a new terminal and navigate to sp25-cis5050-T18/frontend folder. Run "make all" on the folder and then run the shell script: ./start_servers.sh
5. Once everything is running, please connect to http://localhost:8080/ to access PennCloud application
6. Connect to http://localhost:8080/admin/nodes to access the Admin Console

**Please note that when renaming files, click on existing file name, type in new name, and then click outside of the type box once. Do not press enter and click again to prevent errors**

**Screenshots of most impressive parts of application:**

This is an image of our application which applies a appealing appearance and includes an interactive pong game that users can play
![image](https://github.com/user-attachments/assets/8d7bcf80-d73e-4f2d-9453-c9030d6b5afd)


This is an image of our application uploading/downloading a 200MB (big size) file successfully
![WhatsApp Image 2025-05-08 at 17 17 37_e656d305](https://github.com/user-attachments/assets/f815f387-1288-4cd4-b31b-0913d815086f)
![WhatsApp Image 2025-05-08 at 17 20 05_77b08ae7](https://github.com/user-attachments/assets/e3163bea-134c-41fd-b89e-8a4aa8de46cd)


This is an image of our application sending an email to an outside address (@seas.penn.edu) and it being received properly
![WhatsApp Image 2025-05-08 at 17 59 42_5cbd8cf9](https://github.com/user-attachments/assets/a21dbe56-b0a3-40b0-b272-befb6479f86b)
![WhatsApp Image 2025-05-08 at 17 59 51_ccc524bf](https://github.com/user-attachments/assets/3d6eec14-9a30-4cb9-8cbf-5914de4d5074)


This is an image of our application's webstorage page which allows the user to implement file hiearchy and delete files recursively

<img width="840" alt="image" src="https://github.com/user-attachments/assets/1dc078e1-352b-4038-bf18-561d843ba5e1" />
<img width="840" alt="image" src="https://github.com/user-attachments/assets/7faaae20-aa10-4471-a698-9e73c2ae3749" />
<img width="840" alt="image" src="https://github.com/user-attachments/assets/4ad36d3e-4294-47e0-80ce-714ce0ac5769" />
<img width="840" alt="image" src="https://github.com/user-attachments/assets/d0f84121-c91d-4270-95ca-883b4d22e5ab" />



This is an image of our application's admin console which allows the administrator to kill and restart nodes and also view the data on each shard at any point
![image](https://github.com/user-attachments/assets/fc141d51-4d6d-47ea-91a6-077a85a95cad)
![image](https://github.com/user-attachments/assets/dbf02c9b-5267-4529-9ff6-a7367512b63e)



**Please see the README files in each folder for further guidance/configurations and explanation of features**

Full name:  Shutong Jiang, Minna Kuriakose, Lu Men, Steven Su

SEAS login: ryjiang, mkuri, menlu, hanqisu

Which features did you implement? 
  (list features, or write 'Entire assignment')
Entire assignment

Did you complete any extra-credit tasks? If so, which ones?
  (list extra-credit tasks)
  1. Visually appealing user interface (UI) with CSS.
  2. Support very big files like 200MB.
  3. An interesting frontend game.

Did you personally write _all_ the code you are submitting
(other than code from the course web page)?
  [x] Yes
  [ ] No

Did you copy any code from the Internet, or from classmates?
  [ ] Yes
  [x] No

Did you collaborate with anyone on this assignment?
  [ ] Yes
  [x] No

Did you use an AI tool such as ChatGPT for this assignment?
  [ ] Yes
  [x] No 
