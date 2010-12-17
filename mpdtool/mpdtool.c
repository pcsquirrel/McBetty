/* Tool to receive mpd commands over serial line and to send back responses */

/*
	Application Layer:
	We do have a half-duplex communication between mpd and its clients.
	All communications are initiated by the clients.
	They send commands to mpd, to which it responds with answers (and actions).
	
	Commands are either single line commands (which end with '\n' )
	or multiple line messages which start with "command_list_begin" and end with "command_list_end\n".
	
	
	MPD signals the end of its (possibly multiple line) answer with a line beginning with either "OK" or "ACK".
	
	
	Link Layer:
	To use the half duplex communication, we need a way to switch between sending and receiving.
	We will use the EOT (End of Transmission) character (0x04). 
	Each party sends this character as the last character when it has finished sending and is ready to receive.

	This program (mpdtool) knows when mpd has finished sending by interpreting the answer. A line starting with "OK"
	or "ACK" signals the last line from mpd's answer. mpdtool then adds an EOT when sending to serial line. 
	
	The betty firmware knows when it has finished sending a command and then sends the EOT character.
	 
	We further need to signal this program (mpdtool) when the serial link is ready to receive the next bunch of characters.
	This program is supposed to run on fast hardware while the serial link hardware might be slow.
	So after sending <MAX_TX> characters, this program sends an EndOfText <ETX> and only starts sending the next bunch when 
	it receives an ACKNOWLEDGE <ACK> from serial line.
	This might not always work. For some as yet unknown reason the serial line firmware does not get the <ETX> or does not send
	the <ACK>. So if we receive other characters while we are waiting for an <ACK> we are assuming an error and stop waiting.
	
	Transport Layer:
	This program receives commands via serial line. It sends them via a TCP/IP socket to mpd.
	The answers are received via the same socket and transferred back over serial line.
	All commands and answers are non-binary characters (ISO-8859-15 I guess).
	
	We must filter some characters: <ETX> <ACK> and <EOT> are not transmitted in either direction.
	If they occur in the input stream, they are simply dropped.

*/

#define VERSION_MAJOR 1
#define VERSION_MINOR 2

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <termios.h>
#include <unistd.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>

#include <fcntl.h>
#include <sys/time.h>
#include <time.h>

#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>


#define BUFFER_SIZE 1024


#define BAUDRATE B38400

#define _POSIX_SOURCE 1 /* POSIX compliant source */
#define FALSE 0
#define TRUE 1

#define ETX	0x03
#define ACK	0x06
#define EOT	0x04
#define	CR	0x0d
#define LF	0x0a
#define ESC	0x1b

// file descriptor of our serial line
int serial_fd;

/* 
	Global buffers and flags 
*/

char ser_in_buf[BUFFER_SIZE + 1];
int ser_in_len;
int cmd_complete;

char ser_out_buf[BUFFER_SIZE + 1];
int ser_out_len;
static char *ser_out_start;
int wait_ack;

char mpd_line_buf[BUFFER_SIZE + 1];
int mpd_line_len;
int response_line_complete;
int response_finished;


/* resets the serial input buffer */
void
reset_ser_in(){
	ser_in_len = 0;
	cmd_complete = 0;
};

/* resets the serial output buffer */
void
reset_ser_out(){
	ser_out_start = ser_out_buf;
	ser_out_len = 0;
};

// Reset the line buffer for input from mpd
void
reset_mpd_line(){	
	mpd_line_len = 0;
	response_line_complete = 0;
};
	
int min(int x, int y){
	if (x <= y)
		return x;
	return y;
};

/* Time when init_time was called last */
double t0;

void init_time(){
	struct timeval start_time;
	gettimeofday(&start_time, NULL);
	t0 = (double) start_time.tv_sec +  ((double) start_time.tv_usec) / 1000000;
};

/* time since last call of init_time */
double time_diff(){
	struct timeval t1;
	
	gettimeofday(&t1, NULL);
	return ((double) t1.tv_sec) + ((double) t1.tv_usec) / 1000000 - t0;
};

/* Print time since start of program to stderr */
void prt_time(){
	fprintf (stderr,"[%.1lf ] ", time_diff() );
};

/* General routine to send a buffer with bytes_to_send bytes to the given file descriptor.
	Handles short writes, so that either all bytes are successfully written
	or an error message is printed.
	Returns TRUE iff successful, else 0.
*/
int 
write_all(int fd, char *buffer, int bytes_to_send){
	int bytes_written;
	int num = 0;
	
	for (bytes_written = 0; bytes_written < bytes_to_send; bytes_written += num)
	{
		/* Write all remaining bytes to fd at once (if possible). */
		num = write(fd, (void *)(buffer + bytes_written), (bytes_to_send - bytes_written) );
		
		/* Did our write fail completely ? */
		if (num == -1) {
			perror("write_all()");
			return 0;
		};
		
		/* Now num is the number of bytes really written. Can be shorter than expected */
	};
	return 1;
};

/* Read a boot loader response from serial line 
	A response is a single line, terminated by 0x0a
	Returns number of bytes read, if successful
	Returns 0, if  line is longer than BUFFER_SIZE-1
*/
int read_boot_response (int fd, char *buffer){
	int res;		 
	int stop = 0;
	int bytes_read = 0;
	
	do {
		if (bytes_read >= (BUFFER_SIZE - 1)) {
			fprintf(stderr, "Error, too many characters from serial line!\n");
			bytes_read = 0;		// Forget all characters received so far
		};
		
		/* Read at most one character and time out if none received */
		res = read(fd, buffer+bytes_read, 1);
		
		if (res < 0) continue;
		if (res == 0) {
//			fprintf(stderr, "Time out on serial line\n");
			stop = 1;			// Device has finished sending
		} else {
			stop = (buffer[bytes_read] == 0x0a);
//			printf("Received: %02x\n", (unsigned char) buffer[bytes_read]);
			bytes_read += res;
		};
	} while (!stop);
	
	buffer[bytes_read]=0;             /* set end of string, so we have a real string */

	return (bytes_read);
};


/* Send a data record in intel hex format to device 
	Check that it was received correctly.
*/
int send_string(int fd, char *s, char *answer){
	char buffer[BUFFER_SIZE];
	char buffer2[BUFFER_SIZE];
	int cmd_len = strlen(s);
	int i, len;
	
	/* Make sure that all characters are upper case ! */
	for (i=0; i<cmd_len; i++)
		buffer2[i] = toupper(s[i]);
	buffer2[cmd_len] = 0;
	
	write_all(fd, buffer2, cmd_len);
	len = read_boot_response(fd, buffer);
	buffer[len] = 0;
	
	/* Check that our command was echoed correctly */
	if (0 != strncmp(buffer, buffer2, cmd_len)) {	
		printf("Error: Invalid echo!\n");
		printf("%s\n", buffer);
//		return(-20);	
	};
	
	/* buffer[len-1] is 0x0a, buffer[len-2] is 0x0d, buffer[len-3] is '.'  */
	/* The last character should be a '.' */
	if (0 != strncmp(buffer+len-3, ".", 1)) {	
		printf("Error: No . response!\n");
		return (-20);	
	};
	
	/* Print the response characters (if any) */
	for (i=cmd_len; i < (len-3); i++)
		*(answer++) = buffer[i];
	*answer = 0;
	
	return 0;

};

/* 
	Reboot the scart adapter. Just in case everything went wrong.
*/
void reboot_scart(int serial_fd){
	char *s;
	char buffer[BUFFER_SIZE];
	int i, len;
	struct termios oldtio,newtio;
	
	tcgetattr(serial_fd,&oldtio); /* save current port settings */

	bzero(&newtio, sizeof(newtio));

	newtio.c_cflag = B19200 | CS8 | CLOCAL | CREAD;
	newtio.c_iflag = IGNPAR;
	newtio.c_oflag = 0;
	newtio.c_lflag = 0;
 
	newtio.c_cc[VTIME]    = 50;   /* time out after 5 seconds */
	newtio.c_cc[VMIN]     = 0;   /* non-blocking read */
	
	/* now clean the modem line and activate the settings for the port */
	tcflush(serial_fd, TCIFLUSH);
	tcsetattr(serial_fd,TCSANOW,&newtio);
	
	printf("Sending break\n");
	/* Send a break to reset device into ISP mode */
	tcsendbreak(serial_fd,3);

	tcflush(serial_fd, TCIFLUSH);
	
	usleep(1000000);
	tcflush(serial_fd, TCIFLUSH);
	usleep(1000000);
	
	/* Send an uppercase U to negotiate baud rate */
	buffer[0] = 'U';
	buffer[1] = 0;
	
	/* Send U to serial line */
	if (write_all(serial_fd, buffer, 1) == -1) {
		perror("sendall");
	};		
	
	len = read_boot_response(serial_fd, buffer);
	buffer[len] = 0;
	printf("Read %d characters:\n\t", len);
	for (i=0; i<len; i++)
		printf("%02x ", buffer[i]);
	printf("\n");
	
	tcflush(serial_fd, TCIFLUSH);
			
	/* Send U to serial line to check that we are in sync */
	buffer[0] = 'U';
	if (write_all(serial_fd, buffer, 1) == -1) {
		perror("sendall");
	};		
	
	len = read_boot_response(serial_fd, buffer);
	printf("Read %d characters:\n\t", len);
	for (i=0; i<len; i++)
		printf("%02x ", buffer[i]);
	printf("\n");
	
	buffer[len] = 0;

	if (buffer[0] == 'U')
		printf("Baud rate successfully negotiated\n");
	else {
		printf("buffer[0] = %02x\n", buffer[0] );
		printf("Error: Could not negotiate baud rate!\n");
		return;
	};
	
	s=":0100000310EC";
	printf("Manuf. ID \t= ");
	if (0 != send_string(serial_fd,s,buffer) ) {
		return;
	};
	printf("%s\n",buffer);
	if (0 != strncmp(buffer, "15", 2)) {
		fprintf(stderr, "ERROR: Device not recognized\n");
		return;
	};
		
	/* Reset the device */
	printf("\nResetting device\n");
	s=":00000008F8";
	write_all(serial_fd, s, strlen(s));
	read(serial_fd, buffer, 13);		/* Read back the echo and forget it */
	tcflush(serial_fd, TCIOFLUSH);	
	
	/* Restore old serial port settings */
	tcsetattr(serial_fd,TCSANOW,&oldtio);
	
	tcflush(serial_fd, TCIOFLUSH);	
};



/* Convert a string to ISO 8859-15 
	Attention: Length of the string may change !
*/
void
utf8_to_iso8859_15(unsigned char *s){
	unsigned char *rd = s;
	unsigned char *wr = s;
	unsigned char b0, b1;
	unsigned int o1;
	
	while ( (b0 = *rd++) != '\0'){
		if (b0 & 0x80) {
			b1 = *rd;			// we may read next character here, because we have not hit '\0'.
			if ( ((b0 & 0xE0) != 0xC0) || ((b1 & 0xC0) != 0x80) ){
				/* error: not in the range of ISO 8859-15 */
				*wr++ = '?';
				continue;
			};
			/* We now know b1 is not 0. */
			rd++;
			
			o1 = ((b0 & 0x1f) << 6 ) | (b1 & 0x3f);
			if (o1 > 256) o1 = '?';
			*wr++ = o1;
		} else 
			*wr++ = b0;
	};
	*wr = '\0';
};



void
serial_output (char *buf){
		
	while (*buf)
		*(ser_out_start + ser_out_len++) = *(buf++);
}

/*
	Send at most TX_MAX bytes to serial
	Given is ser_out_buf with ser_out_len bytes in it.

	Waits for ACK if MAX_TX bytes have been written.
	Returns when waiting or buffer empty.
*/

#define MAX_TX 16
static int cur_tx_len;

void 
send_to_serial(int serial_fd){
	int bytes_to_send, bytes_written;
	int res;
	int num = 0;
	char ETX_char = ETX;

	static int tx_cnt = 0;
	
	/* Are we still waiting for an ACK? */
	if (wait_ack) return;
	
	bytes_to_send = min (ser_out_len, MAX_TX - tx_cnt);
	
	for (bytes_written = 0; bytes_written < bytes_to_send; bytes_written += num){

		/* Write a certain number of bytes to serial_fd. We either send the remaining bytes in buffer
			or the remaining bytes to fill a packet, whichever is shorter.
		*/
		num = write(serial_fd, (void *)(ser_out_start), bytes_to_send);
		
		/* Did our write fail completely ? */
		if (num == -1) {
			perror("send_to_serial()");
			continue;
		};
		
		/* Now num is the number of bytes really written. Can be shorter than expected */
		ser_out_len -= num;
		ser_out_start += num;
		tx_cnt += num;
	};

	if (tx_cnt >= MAX_TX){
		res = write(serial_fd, &ETX_char, 1);
		if (res == -1) {
			perror("send_to_serial()");
			printf("We could not write ETX!\n");
		} else {
			wait_ack = 1;
		};
		tx_cnt = 0;
	};
	
	return;
};


/* 
	Read some bytes from serial line 
	Sets global flag cmd_finished if EOT is seen.
	Then returns.

	We utilize the fact that Betty sends an EOT when a command is finished.
	Our buffer must be long enough to read multiple lines.
	We can safely assume that commands are below 256 characters (limitation of scart hardware).
	The EOT character will not be included in the returned buffer.
	Null terminates the buffer so that it is a valid C string.
	Sets flag ack_received if a ACK character was received.
	Does not store ACK character.
*/
void 
read_from_serial (int fd){
	int res;		 
	
	res = read(fd, ser_in_buf+ser_in_len, 1);
	if (res == 0) return;
	
	if (res < 0){
		printf("Error on read from serial line, errno = %d\n", errno);
		return;
	};	
	
	switch (ser_in_buf[ser_in_len]) {
		case EOT:
			ser_in_buf[ser_in_len]='\0';			// Null terminate string 
			cmd_complete = 1;					// Set flag
			return;
			
		case ACK:
			wait_ack = 0;
			return;
			 
		default:
			if (ser_in_len < BUFFER_SIZE - 1) 
				ser_in_len++;
			else
				fprintf(stderr, "Error, too many characters from serial line!\n");	
	};	
	return;
};
	

void 
read_from_mpd (int fd){
	int res;		 
	
	res = read(fd, mpd_line_buf+mpd_line_len, 1);
	if (res == 0) return;
	
	if (res < 0){
		printf("Error on read from mpd, errno = %d\n", errno);
		return;
	};	
	
	switch (mpd_line_buf[mpd_line_len]) {
		case '\n':
			mpd_line_buf[++mpd_line_len]=0;			// Null terminate string 
			response_line_complete = 1;			// Set flag
			return;
			
		default:
			if (mpd_line_len < BUFFER_SIZE - 2) 
				mpd_line_len++;
			else
				fprintf(stderr, "Error, too many characters from mpd!\n");	
	};
	return;
};
	
/* 
	Wait for input, either from serial line or from MPD socket
	Uses select so does sleep when nothing to do
*/
int
wait_for_input(int serialfd, int socketfd, int milliseconds){
	struct timeval tv;
	fd_set readfds;
	int numfds;
	int res;
	
	tv.tv_sec = milliseconds / 1000;
	tv.tv_usec = (milliseconds % 1000) * 1000;

	FD_ZERO(&readfds);
	FD_SET(serialfd, &readfds);
	if (socketfd != -1)
		FD_SET(socketfd, &readfds);

	numfds = serialfd + 1;
	if (socketfd > serialfd)
		numfds = socketfd + 1;
	
	// don't care about writefds and exceptfds:
	res = select(numfds, &readfds, NULL, NULL, &tv);
	
	if (res == -1){
		perror("select()");
		return 0;
	};
	
	if (res == 0) {
		return 0;
	};
	
	if (FD_ISSET(serialfd, &readfds))
        read_from_serial(serialfd);
	
	if ((socketfd != -1) && FD_ISSET(socketfd, &readfds) )
        read_from_mpd(socketfd);

    return 1;
} 

/* 
	This routine checks if there is already a valid client_socket established.
	It then returns immediately.
	If the socket is still == -1, it tries to connect to mpd.
	When the connection is successful, the variable *client_socket is set to the connection.
	The content of the serial input buffer is sent to mpd.
	The serial input buffer is reset to allow more input from Betty.
*/
int
send_to_mpd (int *client_socket, struct sockaddr_in *pserverName){
	int i, status, buf_len;
	char buf[BUFFER_SIZE + 1];
	
	buf_len = ser_in_len;
	for (i=0; i < ser_in_len; i++)
		buf[i] = ser_in_buf[i];
	
	reset_ser_in();		// reset the serial input buffer, ready to get next command

	while (-1 == *client_socket){
			
		*client_socket = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (-1 == *client_socket)	{
			perror("socket()");
			return 0;
		};
	
		status = connect(*client_socket, (struct sockaddr*) pserverName, sizeof(*pserverName));
		if (-1 == status){
			perror("connect() to mpd failed");
			fprintf(stderr,"Please check that MPD is running and is accepting connections from another computer!\n");
			fprintf(stderr,"Maybe restarting MPD helps.\n");
			close(*client_socket);
			*client_socket = -1;
			return 0;
		}
 
		// The mpd server responds to a new connection with a version line beginning with "OK"
		reset_mpd_line();

		while (! response_line_complete){
			// Wait 1 second for next MPD response or maybe another command from serial
			status = wait_for_input(serial_fd, *client_socket, 1000);

			// Maybe we were too slow and Betty sent another command
			if (cmd_complete){
				fprintf(stderr, "  Betty sent new command. Sending to MPD cancelled.\n");
				close(*client_socket);
				*client_socket = -1;
				return 0;
			};
			if (status == 0){
				fprintf(stderr, "  No answer when connecting to MPD. Sending to MPD cancelled.\n");
				close(*client_socket);
				*client_socket = -1;
				return 0;
			};	
		};	
		if (strncmp(mpd_line_buf, "OK", 2) != 0) {
			fprintf(stderr,"  Bad initial response from mpd: %s\n", mpd_line_buf);	
			fprintf(stderr, "  Sending to MPD cancelled.\n");
			close(*client_socket);
			*client_socket = -1;
			return 0;
		};
#if 0		
	TODO maybe MPD is waiting for a missing "command_line_end" line.
		Check how that disturbs MPD
		and send this line ourself if necessary
//			sprintf(buffer2, "command_list_end\n");
//			write_all(*client_socket, buffer2, strlen(buffer2));
#endif
	};

	reset_mpd_line();			
	return (write_all(*client_socket, buf, buf_len));
};


/* 
	The MPD protocol is line oriented (terminated by '\n')!
	Normally a single line is a complete command.
	Only when the first line is "command_line_begin\n" does the command end
	when the line "command_line_end\n" is seen.
	
	Our half duplex radio connection adds an extra EOT character at the end of a command.
	We use that here to detect when a command is finished.
	
*/

int main(int argc, char *argv[])
{
	int res, filter_lsinfo, client_socket, remote_port;
	struct hostent *host_ptr = NULL;
	struct sockaddr_in serverName = { 0 };
	char *remote_host = NULL;
	char *serial_device;
	struct termios oldtio,newtio;
	int time_out_lim = 1, time_out_cnt = 0;
	
	
	if (4 != argc)
 	{
		fprintf(stderr, "Usage: %s <serial_device> <serverHost> <serverPort>\n", argv[0]);
		exit(1);
	};
	
	serial_device = argv[1];
	remote_host = argv[2];
	remote_port = atoi(argv[3]);

	/* Initialize time keeping */
	init_time();
	
      /* 
	Open modem device for reading and writing and not as controlling tty
	because we don't want to get killed if linenoise sends CTRL-C.
       */	
	serial_fd = open(serial_device, O_RDWR | O_NOCTTY ); 
	if (serial_fd <0) {perror(serial_device); exit(-1); }
	
	tcgetattr(serial_fd,&oldtio); /* save current port settings */

	bzero(&newtio, sizeof(newtio));
	/* 
		BAUDRATE: Set bps rate. You could also use cfsetispeed and cfsetospeed.
		CRTSCTS : output hardware flow control (only used if the cable has
			all necessary lines. See sect. 7 of Serial-HOWTO)
		CS8     : 8n1 (8bit,no parity,1 stopbit)
		CLOCAL  : local connection, no modem contol
		CREAD   : enable receiving characters
	*/
	newtio.c_cflag = BAUDRATE | CS8 | CLOCAL | CREAD;

	/*
	IGNPAR  : ignore bytes with parity errors
	ICRNL   : map CR to NL (otherwise a CR input on the other computer
	will not terminate input)
	IXOFF    : enable XON/XOFF flow control on input
	otherwise make device raw (no other input processing)
	*/
	newtio.c_iflag = IGNPAR | IXOFF | IXON;

	newtio.c_oflag = 0;			// Raw output.
			
	/* set input mode (non-canonical, no echo,...) 
	disable all echo functionality, and don't send signals to calling program
	*/
	newtio.c_lflag = 0;
 
	newtio.c_cc[VTIME]    = 250;   /* time out after 20 seconds */
	newtio.c_cc[VMIN]     = 0;   /* non-blocking read */
	
	/* now clean the modem line and activate the settings for the port */
	tcflush(serial_fd, TCIFLUSH);
	tcsetattr(serial_fd,TCSANOW,&newtio);
 
	/* Reset the transmitting device so we have a defined state */
//	reset_transmitter(serial_fd);

	/*
	 * need to resolve the remote server name or
	 * IP address 
	*/
	host_ptr = gethostbyname(remote_host);
	if (NULL == host_ptr) {
		host_ptr = gethostbyaddr(remote_host,
				 strlen(remote_host), AF_INET);
		if (NULL == host_ptr) {
			perror("Error resolving server address");
			exit(1);
	 	}
	}

	serverName.sin_family = AF_INET;
	serverName.sin_port = htons(remote_port);
	(void) memcpy(&serverName.sin_addr, host_ptr->h_addr, host_ptr->h_length);

	/*
		This main loop has to be very error tolerant.	
		The idea is to get a command from serial line (terminated by EOT),
		send this command to MPD over TCP socket,
		get the response from MPD (multiple lines), terminated with a line beginning with "OK" or "ACK",
		and send the response to serial line.
		All of these steps may fail:
		No or no complete response from MPD within a short time: 
			Forget it. Send "ACK'EOT'" to serial line to indicate error.
		Could not send command to MPD: 
			Forget it. Send "ACK'EOT'" to serial line to indicate error.
		Serial line could not accept characters fast enough:
			Maybe reset scart adapter ?
		No input from serial line for some time?
			Check if the scart adapter is still alive.
			
		We might not always get synchronous responses, i.e. MPD might send us a belated response,
		while we were waiting for a command from Betty.
			Simply discard those responses.
		Or Betty could send the next command even if we are still waiting for responses from MPD.
			Betty has given up waiting for the answer. So do we.
				
	*/
	
	client_socket = -1;	
	response_line_complete = 0;

	wait_ack = 0;
	cur_tx_len = 0;
	
	reset_ser_in();
	reset_ser_out();
		
	while (1){	
		
		// if nothing to do, wait for some time (61 secs) for input	
		if (! cmd_complete){
			res = wait_for_input(serial_fd, client_socket, 61000);
		
			// if still no input, check if scart adapter (and Betty) is alive.
			if (res == 0) {
				/* No (more) input for some time. Forget all previous bytes */
				fprintf(stderr,"No command from Betty for some time.\n");
				reset_ser_in();
				if (++time_out_cnt >= time_out_lim){
					reboot_scart(serial_fd);
					time_out_cnt = 0;
					time_out_lim *= 2;
				};
				continue;
			};
		};
		// if we got input from MPD here, something must be wrong.
		
		// read more bytes until command is complete
		if (!cmd_complete) continue;
		
		fprintf(stderr, "BETTY: %s", ser_in_buf);
		
		// got a complete input via serial line
		// send it to MPD, if necessary check initial response from mpd
		// resets serial input buffer
		res = send_to_mpd (&client_socket, &serverName);
			
		// NOTE Not transparent transmission. We interpret the messages here.
		// The lsinfo command gets too much information from mpd.
		// Betty only wants to see lines beginning with "playlist:"
		// So we set a flag to filter unwanted lines from transmission over radio link to save bandwidth
		if (0 == strncmp(ser_in_buf, "lsinfo\n", 7))
			filter_lsinfo = 1;
		else 
			filter_lsinfo = 0;
	
		// reset the serial output buffer
		// All previous bytes are not a response to this command
		reset_ser_out();

		reset_mpd_line();
	
		// The response is not finished yet. 
		response_finished = 0;
		init_time();
		
		/* We will break out of this loop if another command from serial is detected */
		while (! response_finished){
			// Poll for MPD response or maybe another command from serial
			res = wait_for_input(serial_fd, client_socket, 0);

			// Maybe we were to slow and Betty sent another command
			if (cmd_complete){
				fprintf(stderr, "  Time out. MPD response cancelled.\n");
				break;
			};
				
			// if there are bytes in the output buffer, send them to serial if it is ready
			send_to_serial(serial_fd);
			
			if (!res){
				if (time_diff() > 10.0)
					fprintf(stderr,"MPD response is too late\n");
			};
			
			if (response_line_complete){

				// Convert line to iso8859-15	
				utf8_to_iso8859_15( (unsigned char *) mpd_line_buf);
				fprintf(stderr, "  MPD: %s", mpd_line_buf);
			
				// If the lsinfo filter is on, we let only 3 types of output lines go through
				if (filter_lsinfo) {
					if ( (strncmp(mpd_line_buf, "playlist:", 9) == 0) ||
			 			(0 == strncmp(mpd_line_buf, "OK", 2)) || (0 == strncmp(mpd_line_buf, "ACK", 3)) )
							serial_output(mpd_line_buf);
				} else {	
					// put the line in serial output buffer	
					serial_output(mpd_line_buf);	
				};
				// check for "OK" or "ACK"
				if ( (0 == strncmp(mpd_line_buf, "OK", 2)) || (0 == strncmp(mpd_line_buf, "ACK", 3)) ){
					response_finished = 1;
					// Send EOT to serial out !
					*(ser_out_start + ser_out_len++) = EOT;
					fprintf(stderr,"\n");
				};
				
				reset_mpd_line();
			};
		};
	
		/* Send out all unsent bytes to serial buffer (as long as there is not another cmd) */
		// TODO potentially infinite waiting time here if scart adapter hangs
		while ( (!cmd_complete)  && (ser_out_len != 0) ){
			// Poll for a new command from serial
			res = wait_for_input(serial_fd, client_socket, 0);
			// if there are bytes in the output buffer, send them to serial if it is ready
			send_to_serial(serial_fd);
		};	
		reset_ser_out();
	};
	

	/* restore the old port settings */
	tcsetattr(serial_fd,TCSANOW,&oldtio);

	return 0;
}