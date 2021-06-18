#include <unistd.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>            //termios, TCSANOW, ECHO, ICANON
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include <sys/stat.h>
#include <ctype.h>
#include <time.h>
const char * sysname = "seashell";

// Group Members: Burcu Özer (64535), Sedat Çoban (60545)

enum return_codes {
	SUCCESS = 0,
	EXIT = 1,
	UNKNOWN = 2,
};
struct command_t {
	char *name;
	bool background;
	bool auto_complete;
	int arg_count;
	char **args;
	char *redirects[3]; // in/out redirection
	struct command_t *next; // for piping
};
/**
 * Prints a command struct
 * @param struct command_t *
 */
void print_command(struct command_t * command)
{
	int i=0;
	printf("Command: <%s>\n", command->name);
	printf("\tIs Background: %s\n", command->background?"yes":"no");
	printf("\tNeeds Auto-complete: %s\n", command->auto_complete?"yes":"no");
	printf("\tRedirects:\n");
	for (i=0;i<3;i++)
		printf("\t\t%d: %s\n", i, command->redirects[i]?command->redirects[i]:"N/A");
	printf("\tArguments (%d):\n", command->arg_count);
	for (i=0;i<command->arg_count;++i)
		printf("\t\tArg %d: %s\n", i, command->args[i]);
	if (command->next)
	{
		printf("\tPiped to:\n");
		print_command(command->next);
	}


}
/**
 * Release allocated memory of a command
 * @param  command [description]
 * @return         [description]
 */
int free_command(struct command_t *command)
{
	if (command->arg_count)
	{
		for (int i=0; i<command->arg_count; ++i)
			free(command->args[i]);
		free(command->args);
	}
	for (int i=0;i<3;++i)
		if (command->redirects[i])
			free(command->redirects[i]);
	if (command->next)
	{
		free_command(command->next);
		command->next=NULL;
	}
	free(command->name);
	free(command);
	return 0;
}
/**
 * Show the command prompt
 * @return [description]
 */
int show_prompt()
{
	char cwd[1024], hostname[1024];
    gethostname(hostname, sizeof(hostname));
	getcwd(cwd, sizeof(cwd));
	printf("%s@%s:%s %s$ ", getenv("USER"), hostname, cwd, sysname);
	return 0;
}
/**
 * Parse a command string into a command struct
 * @param  buf     [description]
 * @param  command [description]
 * @return         0
 */
int parse_command(char *buf, struct command_t *command)
{
	const char *splitters=" \t"; // split at whitespace
	int index, len;
	len=strlen(buf);
	while (len>0 && strchr(splitters, buf[0])!=NULL) // trim left whitespace
	{
		buf++;
		len--;
	}
	while (len>0 && strchr(splitters, buf[len-1])!=NULL)
		buf[--len]=0; // trim right whitespace

	if (len>0 && buf[len-1]=='?') // auto-complete
		command->auto_complete=true;
	if (len>0 && buf[len-1]=='&') // background
		command->background=true;

	char *pch = strtok(buf, splitters);
	command->name=(char *)malloc(strlen(pch)+1);
	if (pch==NULL)
		command->name[0]=0;
	else
		strcpy(command->name, pch);

	command->args=(char **)malloc(sizeof(char *));

	int redirect_index;
	int arg_index=0;
	char temp_buf[1024], *arg;
	while (1)
	{
		// tokenize input on splitters
		pch = strtok(NULL, splitters);
		if (!pch) break;
		arg=temp_buf;
		strcpy(arg, pch);
		len=strlen(arg);

		if (len==0) continue; // empty arg, go for next
		while (len>0 && strchr(splitters, arg[0])!=NULL) // trim left whitespace
		{
			arg++;
			len--;
		}
		while (len>0 && strchr(splitters, arg[len-1])!=NULL) arg[--len]=0; // trim right whitespace
		if (len==0) continue; // empty arg, go for next

		// piping to another command
		if (strcmp(arg, "|")==0)
		{
			struct command_t *c=malloc(sizeof(struct command_t));
			int l=strlen(pch);
			pch[l]=splitters[0]; // restore strtok termination
			index=1;
			while (pch[index]==' ' || pch[index]=='\t') index++; // skip whitespaces

			parse_command(pch+index, c);
			pch[l]=0; // put back strtok termination
			command->next=c;
			continue;
		}

		// background process
		if (strcmp(arg, "&")==0)
			continue; // handled before

		// handle input redirection
		redirect_index=-1;
		if (arg[0]=='<')
			redirect_index=0;
		if (arg[0]=='>')
		{
			if (len>1 && arg[1]=='>')
			{
				redirect_index=2;
				arg++;
				len--;
			}
			else redirect_index=1;
		}
		if (redirect_index != -1)
		{
			command->redirects[redirect_index]=malloc(len);
			strcpy(command->redirects[redirect_index], arg+1);
			continue;
		}

		// normal arguments
		if (len>2 && ((arg[0]=='"' && arg[len-1]=='"')
			|| (arg[0]=='\'' && arg[len-1]=='\''))) // quote wrapped arg
		{
			arg[--len]=0;
			arg++;
		}
		command->args=(char **)realloc(command->args, sizeof(char *)*(arg_index+1));
		command->args[arg_index]=(char *)malloc(len+1);
		strcpy(command->args[arg_index++], arg);
	}
	command->arg_count=arg_index;
	return 0;
}
void prompt_backspace()
{
	putchar(8); // go back 1
	putchar(' '); // write empty over
	putchar(8); // go back 1 again
}
/**
 * Prompt a command from the user
 * @param  buf      [description]
 * @param  buf_size [description]
 * @return          [description]
 */
int prompt(struct command_t *command)
{
	int index=0;
	char c;
	char buf[4096];
	static char oldbuf[4096];

    // tcgetattr gets the parameters of the current terminal
    // STDIN_FILENO will tell tcgetattr that it should write the settings
    // of stdin to oldt
    static struct termios backup_termios, new_termios;
    tcgetattr(STDIN_FILENO, &backup_termios);
    new_termios = backup_termios;
    // ICANON normally takes care that one line at a time will be processed
    // that means it will return if it sees a "\n" or an EOF or an EOL
    new_termios.c_lflag &= ~(ICANON | ECHO); // Also disable automatic echo. We manually echo each char.
    // Those new settings will be set to STDIN
    // TCSANOW tells tcsetattr to change attributes immediately.
    tcsetattr(STDIN_FILENO, TCSANOW, &new_termios);


    //FIXME: backspace is applied before printing chars
	show_prompt();
	int multicode_state=0;
	buf[0]=0;
  	while (1)
  	{
		c=getchar();
		// printf("Keycode: %u\n", c); // DEBUG: uncomment for debugging

		if (c==9) // handle tab
		{
			buf[index++]='?'; // autocomplete
			break;
		}

		if (c==127) // handle backspace
		{
			if (index>0)
			{
				prompt_backspace();
				index--;
			}
			continue;
		}
		if (c==27 && multicode_state==0) // handle multi-code keys
		{
			multicode_state=1;
			continue;
		}
		if (c==91 && multicode_state==1)
		{
			multicode_state=2;
			continue;
		}
		if (c==65 && multicode_state==2) // up arrow
		{
			int i;
			while (index>0)
			{
				prompt_backspace();
				index--;
			}
			for (i=0;oldbuf[i];++i)
			{
				putchar(oldbuf[i]);
				buf[i]=oldbuf[i];
			}
			index=i;
			continue;
		}
		else
			multicode_state=0;

		putchar(c); // echo the character
		buf[index++]=c;
		if (index>=sizeof(buf)-1) break;
		if (c=='\n') // enter key
			break;
		if (c==4) // Ctrl+D
			return EXIT;
  	}
  	if (index>0 && buf[index-1]=='\n') // trim newline from the end
  		index--;
  	buf[index++]=0; // null terminate string

  	strcpy(oldbuf, buf);

  	parse_command(buf, command);

  	// print_command(command); // DEBUG: uncomment for debugging

    // restore the old settings
    tcsetattr(STDIN_FILENO, TCSANOW, &backup_termios);
  	return SUCCESS;
}
int process_command(struct command_t *command);
int main()
{
	while (1)
	{
		struct command_t *command=malloc(sizeof(struct command_t));
		memset(command, 0, sizeof(struct command_t)); // set all bytes to 0

		int code;
		code = prompt(command);
		if (code==EXIT) break;

		code = process_command(command);
		if (code==EXIT) break;

		free_command(command);
	}

	printf("\n");
	return 0;
}

int process_command(struct command_t *command)
{
	//creates file to store history
        char path_history[256];
	strcpy(path_history, getenv("HOME"));
	strcat(path_history, "/history.txt");
	FILE *fhistory = fopen(path_history, "ab");
	if (fhistory == NULL)
	{
    		printf("Error: Could not open the file\n");
    		exit(1);
	}
	
	//hist implementation (part VI)
	//gets the local time
	time_t local_time;
 	struct tm * time_struct;
 	time(&local_time);
 	time_struct = localtime (&local_time);
 	char time_array[256] = {0};
 	strftime(time_array, 256, "%d/%m/%Y %a %X", time_struct); //stores the date, day info
	char history_line[256];
	//stores user, date and command info into a string
	strcpy(history_line, getenv("USER")); 
	strcat(history_line, " ");
	strcat(history_line, time_array);
	strcat(history_line, " ");
	strcat(history_line, command->name);
	strcat(history_line, " ");
	for (int i = 0; i <command->arg_count; i++){
		strcat(history_line, command->args[i]);
		strcat(history_line, " ");
	}
	//prints the history_line to the file
	fprintf(fhistory, "%s\n", history_line);
	fclose(fhistory);
	//creates hist implementation 
	if (strcmp(command->name, "hist")==0)
   	{
        	if (command->arg_count > 0)
        	{	
        		//opens history file
        		fhistory = fopen(path_history, "r");
        		//prints all the history
			if (strcmp(command->args[0], "all")==0){
				char line[256];
		    		while (fscanf(fhistory,"%[^\n]\n", line) != EOF) 
		    		{ 
		  			printf("%s\n", line); 
		   		}
		   	//prints the history of the given user 
		   	}else if (strcmp(command->args[0], "user")==0){
				char line[256];
		    		while (fscanf(fhistory,"%[^\n]\n", line) != EOF) 
		    		{ 
		  			char all_line[256];
		    			strcpy(all_line, line);
			    		char * word = strtok(line, " ");
			    		if(strcmp(word, command->args[1])==0){
			    			printf("%s\n", all_line); 
			  		}
		   		} 
		   	//prints the history of the given date	
		   	}else if (strcmp(command->args[0], "date")==0){
				char line[256];
	    			while (fscanf(fhistory,"%[^\n]\n", line) != EOF) 
	    			{ 	
		    			char all_line[256];
		    			strcpy(all_line, line);
			    		char * word = strtok(line, " ");
			 		word = strtok(NULL, " ");
			    		if(strcmp(word, command->args[1])==0){
			    			printf("%s\n", all_line); 
			  		}
   				}		
		   		} 
		   	//deletes all the history
			if (strcmp(command->args[0], "clear")==0){
				fhistory=freopen(NULL,"w",fhistory);
			}
			fclose(fhistory);
			return SUCCESS;
        	}
        	
        }
	
	int r;
	if (strcmp(command->name, "")==0) return SUCCESS;

	if (strcmp(command->name, "exit")==0)
		return EXIT;

	if (strcmp(command->name, "cd")==0)
	{
		if (command->arg_count > 0)
		{
			r=chdir(command->args[0]);
			if (r==-1)
				printf("-%s: %s: %s\n", sysname, command->name, strerror(errno));
			return SUCCESS;
		}
	}
	
	
	
	
	//shortdir command implementation (Part II)
	if (strcmp(command->name, "shortdir")==0)
   	{
        	if (command->arg_count > 0)
        	{
			char* cwd;
			int PATH_MAX = 1024;
			char buff[PATH_MAX];
			char keys[1024][1024]; //array to store names
			char values[1024][1024]; //array to store directories
			int shortdir_count = 0; //stores the number of associations
			
			struct association{
				char keys[1024][1024];
				char values[1024][1024];
				int shortdir_count;
			}assoc;
			
			//returns the index of a name in the keys array
			int get_index(char *key){
				int i=0;
				for(i = 0; i < shortdir_count; i++){
					if (strcmp(keys[i], key)==0){
						return i;
						}
				} return -1;
			}
			
			//returns the index of a directory in the values array
			int get_value_index(char *value){
				int i=0;
				for(i = 0; i < shortdir_count; i++){
					if (strcmp(values[i], value)==0){
						return i;
						}
				} return -1;
			
			}
			
			//file created to store the associations
			char path_shortdir[256];
			strcpy(path_shortdir, getenv("HOME"));
			strcat(path_shortdir, "/shortdir");
			FILE *fshortdir = fopen(path_shortdir, "r");
			if(fshortdir != NULL){
	    			fread(&assoc, sizeof(assoc), 1, fshortdir);
	    			fclose(fshortdir);
	    		}
	    		//copies the variables of assoc struct
	    		memcpy(keys, assoc.keys, sizeof(keys));
	    		memcpy(values, assoc.values, sizeof(values));
	    		shortdir_count = assoc.shortdir_count;
	    
	    	        //gets current directory	
	    		cwd = getcwd(buff, PATH_MAX);
	    		
			//sets a new name to the current directory
			if (strcmp(command->args[0], "set")==0){
				//if a name is already used, prints a warning
				if(get_index(command->args[1])!=-1){
					printf("%s alias already used\n", command->args[1]);
				}
				//if the same directory already has a name, replaces it
				else if (get_value_index(cwd)!=-1){
					strcpy(keys[get_value_index(cwd)],
					command->args[1]);
					printf("%s is set as an alias for %s\n",
				keys[get_index(command->args[1])],
				values[get_index(command->args[1])]);
				//if the name and directory were not used before, adds them to keys and values
				}else{strcpy(keys[shortdir_count],command->args[1]);
					strcpy(values[shortdir_count],cwd);
					printf("%s is set as an alias for %s\n",
				keys[shortdir_count],values[shortdir_count]);
					shortdir_count++;
				}
				
			}
			//changes to the directory of the name
			if (strcmp(command->args[0], "jump")==0){
				chdir(values[get_index(command->args[1])]);
			}
			//deletes the name-directory association of the given name
			if (strcmp(command->args[0], "del")==0){
				int i=get_index(command->args[1]);
				for(i; i < shortdir_count-1; i++){
					strcpy(keys[i],keys[i+1]);
					strcpy(values[i], values[i+1]);
				}
				shortdir_count--;
			}
			//lists all name-directory associations
			if (strcmp(command->args[0], "list")==0){
				for(int i=0; i < shortdir_count; i++){
					printf("name: %s directory: %s\n", keys[i], values[i]);
				}
			}
			// lists all name-directory associations
			if (strcmp(command->args[0], "clear")==0){
				char keys[1024][1024] = { 0 };
				char values[1024][1024] = { 0 };
				shortdir_count=0;
			}
			
			//copies keys, values and shortdir_count to assoc struct
	    		memcpy(assoc.keys, keys, sizeof(assoc.keys));
	    		memcpy(assoc.values, values, sizeof(assoc.values));
	    		assoc.shortdir_count = shortdir_count;
	    		//writes assoc struct to the file to store associations
			FILE *fshortdir_write = fopen(path_shortdir, "wb");
	    		fwrite(&assoc, sizeof(assoc), 1, fshortdir_write);
	    		fclose(fshortdir_write);
	    		return SUCCESS;

       	}
        	}
        	
        
        
        //goodMorning implementation (Part IV)
	if (strcmp(command->name, "goodMorning")==0)
    {
        if (command->arg_count == 2)
        {
            struct stat i;
            if(stat(command->args[1],  &i)==0){
            	//getting and splitting the given time 
                char delim[] = ".";
                char *time=strtok(command->args[0], delim);            
                char *h;
                h=time;
                time = strtok(NULL,".");
                char *m;
                m=time;
                
                //creating shell script
                FILE *music;
                music = fopen("goodMorning.sh", "w");
                char currentDir[1024];
                getcwd(currentDir, sizeof(currentDir));
                fprintf(music, "DISPLAY=:0 rhythmbox-client --play %s", command->args[1]);
                fclose(music);

		//creating crontab file and executing crontab command with execvp 
		FILE *cron;
		cron =fopen("crontab","w");
		fprintf(cron, "%s %s * * * %s/goodMorning.sh\n",m,h, currentDir);
		fclose(cron);
		char* path=strcat(currentDir,"/crontab");
		char* args[] = {"crontab",path,NULL};
		execvp("crontab", args);
		//execv("/usr/bin/crontab",path);
		
		return SUCCESS;

            }else{
                printf("-%s: %s: %s\n", sysname, command->name, strerror(errno));
            }

        }
    }
    
    //kdiff implementation (Part V)
    if (strcmp(command->name, "kdiff")==0)
    {
        if (command->arg_count == 3)
        {
        	if(strcmp(command->args[0], "-a")==0){ //Line by line comparison
        		struct stat i;
            		if(stat(command->args[1],  &i)==0){ //Check whether first file is exist or not
            			if(stat(command->args[2], &i)==0){ //Check whether second file is exist or not
            			
            				// getting the total line number of the first file
            				FILE *file1;
                			file1 = fopen(command->args[1], "r");
                			if (file1 == NULL){
        					printf("Could not open file %s",command->args[1]);
    					}
    					int ch = getc(file1);
    					int count1=0;
  					while (ch != EOF) 
  					{
  					if(feof(file1)==0){
    						count1++;
    					}
    					ch = getc(file1);
  					}
  					
  					// getting the total line number of the second file
                			FILE *file2;
                			file2 = fopen(command->args[2], "r");
                			if (file2 == NULL){
        					printf("Could not open file %s",command->args[2]);
    					}
    					int ch2 = getc(file2);
    					int count2=0;
  					while (ch2 != EOF) 
  					{
  					if(feof(file2)==0){
    					count2++;
    					}
    					ch2 = getc(file2);
  					}
  					fclose(file1);
  					fclose(file2);
  					
  					//opens the first file for comparison
  					file1 = fopen(command->args[1], "r");
                			if (file1 == NULL){
        					printf("Could not open file %s",command->args[1]);
    					}
    					
    					//opens the second file for comparison
    					file2 = fopen(command->args[2], "r");
                			if (file2 == NULL){
        					printf("Could not open file %s",command->args[2]);
    					}
    					
    					char str1[1000];
    					char str2[1000];
    					int dif = 0; //holds total different line number
    					int max = (count1 > count2 ) ? count1 : count2;
    					for(int i = 0; i< max;i++){
    						fgets(str1, 1000, file1); //getting ith line of the first file
    						fgets(str2, 1000, file2); //getting ith line of the second file
    						
    						if(feof(file1) && feof(file2)){
    							continue;
    						}
    						
    						int str1Len = strlen(str1);
    						int str2Len = strlen(str2);
    						int found = 0;
    						
    						//Comparison starts
    						if(str1 != "" && str2 !=""){
    							if(str1Len>=str2Len){
    								found=0;
    								for (int j = 0; j<str1Len; j++){
    									if(str1[j] == str2[j]){ //compares char by char of the lines.
 										found++;
 									}
    								}

    								if(found!=str1Len){
    									if(feof(file1)){
    										printf("%s:Line %d:%s",command->args[1],i+1,"\n"); //If the first file's line is empty then print newline.
    									}else{
    										printf("%s:Line %d:%s",command->args[1],i+1,str1); //If the first file's line is not empty then print that line.
    									}

    									printf("%s:Line %d:%s",command->args[2],i+1,str2);
    									dif++;
    								}
    							}else{
    								found=0;
    								for (int j = 0; j<str2Len; j++){ //compares char by char of the lines.
    									if(str1[j] == str2[j]){
 										found++;
 									}
    								}

    								if(found!=str2Len){
    									printf("%s:Line %d:%s",command->args[1],i+1,str1);
    									if(feof(file2)){
    										printf("%s:Line %d:%s",command->args[2],i+1,"\n"); //If the second file's line is empty then print newline.
    									}else{
    										printf("%s:Line %d:%s",command->args[2],i+1,str2); //If the second file's line is not empty then print that line.
    									}
    									
    									dif++;
    								}
    							}
    						}
    					}
    					
    					if(dif!=0){
    						printf("%d different lines are found\n",dif);
    					}else{
    						printf("Files are identical\n");
    					}
    					
                			
            			fclose(file1);
            			fclose(file2);
            			return SUCCESS;
            			
            			}else{
            				printf("This file is not found: %s\n",command->args[2]);
            			}
        		}else{
        			printf("This file is not found: %s\n",command->args[1]);
        		}
        		
        	}else if(strcmp(command->args[0], "-b")==0){ //binary comparison of two files
        		struct stat i;
        		if(stat(command->args[1],  &i)==0){ //Check whether first file is exist or not
            			if(stat(command->args[2], &i)==0){ //Check whether second file is exist or not
            			
            				// getting the total line number of the first file
            				FILE *file1;
                			file1 = fopen(command->args[1], "rb");
                			if (file1 == NULL){
        					printf("Could not open file %s",command->args[1]);
    					}
    					int ch = getc(file1);
    					int count1=0;
  					while (ch != EOF) 
  					{
  					if(feof(file1)==0){
    						count1++;
    					}
    					ch = getc(file1);
  					}
    					
    					// getting the total line number of the second file
    					FILE *file2;
                			file2 = fopen(command->args[2], "r");
                			if (file2 == NULL){
        					printf("Could not open file %s",command->args[2]);
    					}
    					int ch2 = getc(file2);
    					int count2=0;
  					while (ch2 != EOF) 
  					{
  					if(feof(file2)==0){
    					count2++;
    					}
    					ch2 = getc(file2);
  					}
  					
  					fclose(file1);
  					fclose(file2);
  					
  					//opens the first file for comparison
  					file1 = fopen(command->args[1], "rb");
                			if (file1 == NULL){
        					printf("Could not open file %s",command->args[1]);
    					}
    					
    					//opens the second file for comparison
    					file2 = fopen(command->args[2], "rb");
                			if (file2 == NULL){
        					printf("Could not open file %s",command->args[2]);
    					}
  					
  					int dif = 0;
    					int max = (count1 > count2 ) ? count1 : count2;
    					int c1 = 0; // for first file's binary
    					int c2 = 0; // for second file's binary
    					for(int i = 0; i< max;i++){ //travel through the files to compare the binaries
    						c1 = fgetc(file1);
    						c2 = fgetc(file2);
    						
    						if(feof(file1) && feof(file2)){
    							continue;
    						}
    						
    						if(c1!=c2){ //If the binaries' are not equal, increase difference by 1
    							dif = dif+1;
    						}
    					}
    					
    					if(dif!=0){
    						printf("The two files are different in %d bytes\n",dif);
    					}else{
    						printf("Files are identical\n");
    					}
    					
    					
    					return SUCCESS;
    				}else{
            				printf("This file is not found: %s\n",command->args[2]);
            			}
        		}else{
        			printf("This file is not found: %s\n",command->args[1]);
        		}
        		}
   
            }else if(command->arg_count == 2){ //line by line comparison when -a is not entered as an input
            	struct stat i;
            		if(stat(command->args[0],  &i)==0){ //Check whether first file is exist or not
            			if(stat(command->args[1], &i)==0){ //Check whether second file is exist or not
            			
            				// getting the total line number of the first file
            				FILE *file1;
                			file1 = fopen(command->args[0], "r");
                			if (file1 == NULL){
        					printf("Could not open file %s",command->args[0]);
    					}
    					int ch = getc(file1);
    					int count1=0;
  					while (ch != EOF) 
  					{
  					if(feof(file1)==0){
  					
    						count1++;
    					}
    					ch = getc(file1);
  					}
  					
  					// getting the total line number of the second file
                			FILE *file2;
                			file2 = fopen(command->args[1], "r");
                			if (file2 == NULL){
        					printf("Could not open file %s",command->args[1]);
    					}
    					int ch2 = getc(file2);
    					int count2=0;
  					while (ch2 != EOF) 
  					{
  					if(feof(file2)==0){
    					count2++;
    					}
    					ch2 = getc(file2);
  					}
  					
  					fclose(file1);
  					fclose(file2);
  					
  					//opens the first file for comparison
  					file1 = fopen(command->args[0], "r");
                			if (file1 == NULL){
        					printf("Could not open file %s",command->args[0]);
    					}
    					
    					//opens the second file for comparison
    					file2 = fopen(command->args[1], "r");
                			if (file2 == NULL){
        					printf("Could not open file %s",command->args[1]);
    					}
    					
    					
    					char str1[1000];
    					char str2[1000];
    					int dif = 0; //holds total different line number
    					int max = (count1 > count2 ) ? count1 : count2;
    					for(int i = 0; i< max;i++){
    						fgets(str1, 1000, file1); //getting ith line of the first file
    						fgets(str2, 1000, file2); //getting ith line of the second file
    						if(feof(file1) && feof(file2)){
    							continue;
    						}
    						int str1Len = strlen(str1);
    						int str2Len = strlen(str2);
    						int found = 0;
    						
    						//Comparison starts
    						if(str1 != "" && str2 !=""){ 
    							if(str1Len>=str2Len){
    								found=0;
    								for (int j = 0; j<str1Len; j++){
    									if(str1[j] == str2[j]){ //compares char by char of the lines.
 										found++;
 									}
    								}

    								if(found!=str1Len){
    									if(feof(file1)){
    										printf("%s:Line %d:%s",command->args[0],i+1,"\n"); //If the first file's line is empty then print newline.
    									}else{
    										printf("%s:Line %d:%s",command->args[0],i+1,str1); //If the first file's line is not empty then print that line.
    									}

    									printf("%s:Line %d:%s",command->args[1],i+1,str2);
    									dif++;
    								}
    							}else{
    								found=0;
    								for (int j = 0; j<str2Len; j++){
    									if(str1[j] == str2[j]){
 										found++;
 									}
    								}

    								if(found!=str2Len){
    									printf("%s:Line %d:%s",command->args[0],i+1,str1);
    									if(feof(file2)){
    										printf("%s:Line %d:%s",command->args[1],i+1,"\n"); //If the second file's line is empty then print newline.
    									}else{
    										printf("%s:Line %d:%s",command->args[1],i+1,str2); //If the second file's line is not empty then print that line.
    									}
    									
    									dif++;
    								}
    							}
    						}
    					}
    					
    					if(dif!=0){
    						printf("%d different lines are found\n",dif);
    					}else{
    						printf("Files are identical\n");
    					}
    					
                			
            			fclose(file1);
            			fclose(file2);
            			return SUCCESS;
            			}else{
            				printf("This file is not found: %s\n",command->args[1]);
            			}
        		}else{
        			printf("This file is not found: %s\n",command->args[0]);
        		}
            	
            
            }else{
                printf("-%s: %s: %s\n", sysname, command->name, strerror(errno));
            }

        }
    
    //highlight implementation (Part III)
    if (strcmp(command->name, "highlight")==0){
		
		if (command->arg_count == 3)
        	{
        	
            	 struct stat i;
           	 if(stat(command->args[2],  &i)==0){ //checks whether the file is exist or not
           	 
           	 	//opens the file
           	 	FILE *file;
			char str[1000];
			file = fopen(command->args[2], "r");
    			if (file == NULL){
        			printf("Could not open file %s",command->args[2]);
    			}
    			
    			//reads the file
 			while (fgets(str, 1000, file) != NULL){
 				
 				int lineLength = strlen(str);
 				int wordLength = strlen(command->args[0]);
 				int occurence[1000] = {[0 ... 999] = -1}; //holds the location of the word in the file's line
 				int count = 0;
				int found = 1;
				
 				for (int i = 0 ; i < lineLength - wordLength; i++){ //travels through the line
 					found = 0;
 					for (int j = 0; j<wordLength; j++){ //compares the line subpart with the word.
 						if(tolower(str[i+j]) == tolower(command->args[0][j])){
 							found++;
 						}
 					}
 					
 					if(i==0){ //checks whether the word is the first word in the line.
 						if(i+wordLength<=lineLength){ 

 							if((str[i+wordLength] == ' ') || str[i+wordLength] == '\n'){ //checks whether the character after the word is space or newline.

 								if(found==wordLength){ //If the found is equal to word's length, we found our word in the line.
 									occurence[count] = i;
 									count++;
 								}
 							
 							}
 							
 							
 						}
 					}else if(str[i-1]==' '){ //checks whether the character before the word is space or not.
 						if(i+wordLength<=lineLength){

 							if((str[i+wordLength] == ' ') || str[i+wordLength] == '\n'){ //checks whether the character after the word is space or newline.

 								if(found==wordLength){ //If the found is equal to word's length, we found our word in the line.
 									occurence[count] = i;
 									count++;
 								}
 							
 							}
 						}
 					}else{
 						
 					}
 					
 				}
 				
 				if(occurence[0] != -1){ //checks whether you found the word in the line or not.
 					int contains = 0;
 					for (int k = 0; k < lineLength ; k++){
 						contains = 0;
 						for(int m = 0; m<lineLength; m++){
 							if(occurence[m] == k) {
 								contains = 1;
 							}
 						}
 						if(contains == 1){
 							if(strcmp(command->args[1], "r")==0){
 								printf("\033[0;31m");	//to write in red
 							}else if (strcmp(command->args[1], "g")==0){
 								printf("\033[32m"); //to write in green
 							
 							}else if (strcmp(command->args[1], "b")==0){
 								printf("\033[0;34m"); //to write in blue
 							}
 							for(int z = k; z<k+wordLength; z++) { //prints the word
 								printf("%c",str[z]);
 							}
 							
 							k = k + wordLength-1; //updating k in order to pass the word.
 						}else{
 							printf("\033[0m"); //reset the color
 							printf("%c", str[k]); 
 						}
 						
 						
 					}
 					printf("\033[0m"); //reset the color
 					
 				}
 			}
 			
        		fclose(file);
        		return SUCCESS;
           	 }
           	}
	}

	pid_t pid=fork();
	if (pid==0) // child
	{
		/// This shows how to do exec with environ (but is not available on MacOs)
	    // extern char** environ; // environment variables
		// execvpe(command->name, command->args, environ); // exec+args+path+environ

		/// This shows how to do exec with auto-path resolve
		// add a NULL argument to the end of args, and the name to the beginning
		// as required by exec

		// increase args size by 2
		command->args=(char **)realloc(
			command->args, sizeof(char *)*(command->arg_count+=2));

		// shift everything forward by 1
		for (int i=command->arg_count-2;i>0;--i)
			command->args[i]=command->args[i-1];

		// set args[0] as a copy of name
		command->args[0]=strdup(command->name);
		// set args[arg_count-1] (last) to NULL
		command->args[command->arg_count-1]=NULL;

		//To get path of the command and prints it into the pathToCommand file (Part I)
		char msgOut[1024]="";
		strcat(msgOut, "which ");
		strcat(msgOut, command->args[0]);
		strcat(msgOut, "> pathToCommand");
		system(msgOut);
		FILE *file;
		char str[1000];
		file = fopen("pathToCommand", "r");
    		if (file == NULL){
        		printf("Could not open file %s","pathToCommand");
    		}
    		
 		while (fgets(str, 1000, file) != NULL) //getting the path from the file.
        	fclose(file);
        	
        	
  		static char str1[99];
    		int count = 0, j, k;
  
    		
   		while (str[count] == ' ') { //finding the first apperance of the space
    		    count++;
    		    printf("%d\n",count);
   		}
  
   		
    		for (j = count, k = 0; str[j] != '\0'; j++, k++) { //To get only the written part of the line and copying it to another variable.
        		str1[k] = str[j];
    		}
  
    
		str1[strcspn(str1, "\n")] = 0; //To put null into the position of new line.
		remove ("pathToCommand"); //removing the file.
  		execv(str1, command->args); //executing the command.
  		
		//execvp(command->name, command->args); // exec+args+path
		
		exit(0);
		/// TODO: do your own exec with path resolving using execv()
		
		
		
	}
	else
	{
		if (!command->background)
			wait(0); // wait for child process to finish
		return SUCCESS;
	}

	// TODO: your implementation here
	
	
	
	
	printf("-%s: %s: command not found\n", sysname, command->name);
	return UNKNOWN;
}
