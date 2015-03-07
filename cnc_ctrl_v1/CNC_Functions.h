    /*This file is part of the Makesmith Control Software.

    The Makesmith Control Software is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    Makesmith Control Software is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with the Makesmith Control Software.  If not, see <http://www.gnu.org/licenses/>.
	
	Copyright 2014 Bar Smith*/

#include "MyTypes.h"

#define FORWARD 1
#define BACKWARD -1

//mm per rotation
#define XPITCH (25.4 / 20)
#define YPITCH (25.4 / 20)
#define ZPITCH (25.4 / 20)

#define FREESPEED 105 /*Max free-running rpm at 5V. Max speeds are computed from this since this is easy to interpolate from the specs usually given for servos.*/

#define XDIRECTION BACKWARD
#define YDIRECTION BACKWARD
#define ZDIRECTION BACKWARD

#define TIMESTEP 10 //Interval at which motor speeds are adjusted, in ms.

#define XSTOP 90
#define YSTOP 90
#define ZSTOP 90

#define XSERVO 5
#define YSERVO 6
#define ZSERVO 7

#define SENSEPIN 53

#define TOLERANCE .3//this sets how close to the target point the tool must be before it moves on.
#define MOVETOLERANCE .2 //this sets how close the machine must be to the target line at any given moment

#define MAXSPEED (0.8*FREESPEED) //rpm Even when not cutting, there is some friction. These give the maximum rate _per axis:_ Diagonal cutting will be faster.
#define MAXCUTSPEED (0.6*FREESPEED) //rpm

#define KP 400 //gain, in preparation for PID. Multiplies speed error in revolutions per time step
#define KPP 123 //also gain---there are two values in the present code
#define KPX KP
#define KPY KP
#define KPZ KP
#define KIX 0 //Multiplies sum of errors
#define KIY 0
#define KIZ 0
#define KDX 0 //Multiplies change in velocity, not error, in revolutions per timestep per timestep
#define KDY 0
#define KDZ 0

int feedrate
float XunitScalar = (1/XPITCH);
float YunitScalar = (1/YPITCH);
float ZunitScalar = (1/ZPITCH);
location_st location = {0.0 , 0.0 , 0.0 , 0.0 , 0.0 , 0.0 , 500 , 500 , 500, 0, 0, 0}; 
int xpot = 8;
int ypot = 9;
int zpot = 10;
Servo x;
Servo y;
Servo z;
int servoDetachFlag = 1;
int movemode = 1; //if move mode == 0 in relative mode,   == 1 in absolute mode
float xMagnetScale = 1.23;
float yMagnetScale = 1.23;
float zMagnetScale = 1.23;



/*PWMread() measures the duty cycle of a PWM signal on the provided pin. It then
takes this duration and converts it to a ten bit number.*/
int PWMread(int pin){
	int duration = 0;
	float tempMagnetScale = 1.23;
	if (pin == xpot){
		tempMagnetScale = xMagnetScale;
	}
	if (pin == ypot){
		tempMagnetScale = yMagnetScale;
	}
	if (pin == zpot){
		tempMagnetScale = zMagnetScale;
	}
	
	
	duration = pulseIn(pin, HIGH, 2000); //This returns the pulse duration
	duration = (int)((float)duration*tempMagnetScale); //1.23 scales it to a ten bit number
	
	if (duration >= 1023){
		duration = 1023;
	}
	
	if (duration < 10){
		duration = 0;
	}
	return duration;
}

float getAngle(float X,float Y,float centerX,float centerY){
	/*This function takes in the ABSOLUTE coordinates of the end of the circle and
 the ABSOLUTE coordinates of the center of the circle and returns the angle between the end and the axis in pi-radians*/
	
	float theta = 0;
	if ( abs(X - centerX) < .1){
		centerX = X;
	}
	if ( abs(Y - centerY) < .1){
		centerY = Y;
	}
	
	if (X == centerX) { //this resolves div/0 errors
		if (Y >= centerY) {
			//Serial.println("special case one");
			return(0.5);
		}
		if ( Y <= centerY ){
			//Serial.println("special case two");
			return(1.5);
		}
	}
	if (Y == centerY) { //this resolves div/0 errors
		if ( X >= centerX) {
			//Serial.println("special case three");
			return(0);
		}
		if (X <= centerX) {
			//Serial.println("special case four");
			return(1.0);
		}
	}
	if (X > centerX and Y > centerY) { //quadrant one
		//Serial.println("Quadrant 1");
		theta = atan((centerY - Y)/(X - centerX));
		theta = 2 + theta/3.141593;
	}
	if (X < centerX and Y > centerY) { //#quadrant two
		//Serial.println("Quadrant 2");
		theta = atan((Y - centerY)/(X - centerX));
		theta = 1 - theta/3.141593;
	}
	if (X < centerX and Y < centerY) { //#quadrant three
		//Serial.println("Quadrant 3");
		theta = atan((centerY - Y)/(centerX - X));
		//Serial.println(theta);
		//theta = theta/3.141593 + 1;
		theta = 2 - theta/3.141593;
		theta = theta - 1;
		//Serial.println(theta);
	}
	if (X > centerX and Y < centerY) { //#quadrant four
		//Serial.println("Quadrant 4");
		theta = atan((centerY - Y)/(X - centerX));
		//Serial.println(theta);
		theta = theta/3.141593;
		//Serial.println(theta);
	}
	
	theta = 2 - theta;
	
	//Serial.println("Theta: ");
	//Serial.println(theta);
	return(theta);
}

/*SetScreen() updates the position displayed on the LCD display (yes, we're working on adding an LCD display upgrade.*/
/*void SetScreen(float x, float y, float z){
	char Msg[6];
	
	clearDisplay(WHITE); 
	setStr("X: ", 0, 0, BLACK);
	setStr("Y: ", 0, 10, BLACK);
	setStr("Z: ", 0, 20, BLACK);
	
	if(x < 0){
		setStr("-", 14, 0, BLACK);
	}
	if(y < 0){
		setStr("-", 14, 10, BLACK);
	}
	if(z < 0){
		setStr("-", 14, 20, BLACK);
	}
	
	int t100 = 100 * abs(x); 
	int tWhole = t100 / 100;
	int tFract = t100 % 100;
	sprintf(Msg, "%d.%d", tWhole, tFract < 10 ? 0 : tFract);
	setStr(Msg, 20, 0, BLACK);
	
	t100 = 100 * abs(y); 
	tWhole = t100 / 100;
	tFract = t100 % 100;
	sprintf(Msg, "%d.%d", tWhole, tFract < 10 ? 0 : tFract);
	setStr(Msg, 20, 10, BLACK);
	
	t100 = 100 * abs(z); 
	tWhole = t100 / 100;
	tFract = t100 % 100;
	sprintf(Msg, "%d.%d", tWhole, tFract < 10 ? 0 : tFract);
	setStr(Msg, 20, 20, BLACK);

	if (XunitScalar == 25.4 * XPITCH){
		setStr("G20", 67, 0, BLACK);
	}
	else{
		setStr("G19", 67, 0, BLACK);
	}
	
	if (movemode == 1){
		setStr("G90", 67, 10, BLACK);
	}
	else{
		setStr("91", 67, 10, BLACK);
	}
	
	setStr("Makesmith Tech", 0, 40, BLACK);
	
	
	updateDisplay();
}*/

/*The SetPos() function updates the machine's position by essentially integrating the input from the encoder*/
int SetPos(location_st* position){
	int maxJump = 400; 
	static int loopCount = 0;
	static int CurrentXangle, CurrentYangle, CurrentZangle;
	static int PreviousXangle, PreviousYangle, PreviousZangle;
	
	if(abs(CurrentXangle - PreviousXangle) <= maxJump){ //The encoder did not just transition from 0 to 360 degrees
		position->xpos = position->xpos + (CurrentXangle - PreviousXangle)/1023.0; //The position is incremented by the change in position since the last update.
	}
	else{//The transition from 0 to 360 (10-bit value 1023) or vice versa has just taken place
		if(PreviousXangle < 200 && CurrentXangle > 850){ //Add back in any dropped position
			CurrentXangle = 1023;
			position->xpos = position->xpos + (0 - PreviousXangle)/1023.0;
		}
		if(PreviousXangle > 850 && CurrentXangle < 200){
			CurrentXangle = 0;
			position->xpos = position->xpos + (1023 - PreviousXangle)/1023.0;
		}
	}
	if(abs(CurrentYangle - PreviousYangle) <= maxJump){
		position->ypos = position->ypos + (CurrentYangle - PreviousYangle)/1023.0;
	}
	else{
		if(PreviousYangle < 200 && CurrentYangle > 850){
			CurrentYangle = 1023;
			position->ypos = position->ypos + (0 - PreviousYangle)/1023.0;
		}
		if(PreviousYangle > 850 && CurrentYangle < 200){
			CurrentYangle = 0;
			position->ypos = position->ypos + (1023 - PreviousYangle)/1023.0;
		}
	}
	if(abs(CurrentZangle - PreviousZangle) <= maxJump){
		position->zpos = position->zpos + (CurrentZangle - PreviousZangle)/1023.0;
	}
	else{
		if(PreviousZangle < 200 && CurrentZangle > 850){
			CurrentZangle = 1023;
			position->zpos = position->zpos + (0 - PreviousZangle)/1023.0;
		}
		if(PreviousZangle > 850 && CurrentZangle < 200){
			CurrentZangle = 0;
			position->zpos = position->zpos + (1023 - PreviousZangle)/1023.0;
		}
	}
	
	PreviousXangle = CurrentXangle; //Reset the previous angle variables
	PreviousYangle = CurrentYangle;
	PreviousZangle = CurrentZangle;
	
	if(XDIRECTION == FORWARD){ //Update the current angle variable. Direction is set at compile time depending on which side of the rod the encoder is positioned on.
		CurrentXangle = PWMread(xpot);
	}
	else{
		CurrentXangle = 1023 - PWMread(xpot);
	}
	
	if(YDIRECTION == FORWARD){
		CurrentYangle = PWMread(ypot);
	}
	else{
		CurrentYangle = 1023 - PWMread(ypot);
	}
	
	if(ZDIRECTION == FORWARD){
		CurrentZangle = PWMread(zpot);
	}
	else{
		CurrentZangle = 1023 - PWMread(zpot);
	}
	
	
	loopCount++;
	if(loopCount > 30){ //Update the position every so often
		if(servoDetachFlag == 0){ //If the machine is moving print the real position
			Serial.print("pz(");
			Serial.print(position->xpos);
			Serial.print(",");
			Serial.print(position->ypos);
			Serial.print(",");
			Serial.print(position->zpos);
			Serial.println(")");
			//SetScreen(position->xpos / XunitScalar, position->ypos / YunitScalar, position->zpos / ZunitScalar);
		}
		else{ //If the machine is stopped print the target position
			Serial.print("pz(");
			Serial.print(position->xtarget);
			Serial.print(",");
			Serial.print(position->ytarget);
			Serial.print(",");
			Serial.print(position->ztarget);
			Serial.println(")");
			//SetScreen(position->xtarget / XunitScalar, position->ytarget / YunitScalar, position->ztarget / ZunitScalar);
		} 
		loopCount = 0;
	}
}

/*BoostLimit sets the upper and lower bounds of the signals which go to the servos to prevent weird behavior. Valid input to set the servo speed ranges from 0-180, and the Arduino servo library gives strange results if you go outside those limits.*/
int BoostLimit(int boost, int limit){
	if(boost > limit){
		boost = limit;
	}
	if(boost < -limit){
		boost = -limit;
	}
	return (boost);
}

/*SetSpeed() takes a position and a target and sets the speed of the servo to hit that target. Right now it implements a proportional controller, where the gain is set by the 'gain' input. A PID controller would be better.*/
int SetSpeed(float posNow, float posTarget, int gain){
	int speed;
	
	speed = gain * (posTarget - posNow); //Set speed proportional to the distance from the target
	
	if(abs(posNow - posTarget) < .02){ //Set the deadband
		speed = 0;
	}
	
	speed = BoostLimit(speed, 85); //Limits the output to an acceptable range
	
	return(speed);
}

/*PIDSetSpeed() takes a position and a target and sets the speed of the servo to hit that target. 
posStart is the location _in rotations_ at the beginning of the previous tick.
posTarget is the location _in rotations_ the machine is supposed to be going now in one tick.
new resets the static values when a new line is begun or the feedrate changes.*/
int PIDSetSpeed(float posNow, float posTarget, int kp, int ki, int kd, int new){
  float speedChange = 0.0;
  static float previousError = 0.0;
  static float posStart = posNow;
  static float previousPosStart = 0.0;
  static float error = 0.0;
  static float errorSum = 0.0;  
  float speed;

  if( new ){
  previousError = 0.0;
  posStart = posNow;
  previousPosStart = posNow;
  error = 0.0;
  errorSum = 0.0;
  new = 0;
  }

  previousError = error;
  error = (posTarget - posNow) - (posNow - previousPosStart); //The first term is the distance that should have been moved; the second, the distance actually moved.
  errorSum = errorSum + error;
  speedChange = (posNow - posStart) - (posStart - previousPosStart);
  previousPosStart = posStart;
  posStart = posNow;
	
  speed = (kp * error) + (ki * errorSum) + (kd * speedChange);
	
	if(abs(error) < .02){ //Set the deadband
		speed = 0;
	}
	
	speed = BoostLimit(speed, 85); //Limits the output to an acceptable range
	
	return(speed);
}

/*The SetTarget() function attempts to move the machine to the Target position (mm) in one tick.*/
int SetTarget(float xTarget, float yTarget, float zTarget, location_st* position){
	int xspeed, yspeed, zspeed;
	xspeed = PIDSetSpeed(location.xpos, (xTarget * XunitScalar), KPX, KIX, KDX, 0); //Generate motor speeds
	yspeed = PIDSetSpeed(location.ypos, (yTarget * YunitScalar), KPY, KIY, KDY, 0);
	zspeed = PIDSetSpeed(location.zpos, (zTarget * ZunitScalar), KPZ, KIZ, KDZ, 0);
	
	x.write(90 + XDIRECTION*xspeed); //Command the motors to rotate
	y.write(90 + YDIRECTION*yspeed);
	z.write(90 + ZDIRECTION*zspeed);
}

/*The Unstick() function is called to attempt to unstick the machine when it becomes stuck. */
int Unstick(Servo axis, int direction){
	static long staticTime = millis(); //This variable holds the time the function was last called. It persists between function calls.
	static int count = 0; //count is used to determine if the machine has become seriously stuck or if the machine is able to free itself. If the unstick() function is called multiple times in a short span of time the machine is deemed to be permanently stuck.
	
	if(millis() - staticTime < 1000){ 
		count++;
	}
	else{
		count = 0;
	}
	
	axis.write(90 + 45*direction); //Spin the motor backwards
	long tmptime = millis();
	while(millis() - tmptime < 30){ //This is just a delay which doesn't lose the machine's position.
		SetPos(&location); 
	}
	axis.write(90 - 45*direction); //Spin the motor forward again
	tmptime = millis();
	while(millis() - tmptime < 140){
		SetPos(&location); 
	}
	
	staticTime = millis();//sets the time the last function finished
	
	if (count > 15){ //The machine is seriously stuck
		Serial.println("really stuck");
		x.detach(); //Detach the motors to prevent them from being damaged
		y.detach();
		z.detach();
		String stuckString = "";
		while(1){ //Wait for signal to continue
			SetPos(&location);
			if (Serial.available() > 0) {
				char c = Serial.read();  
				stuckString += c; 
			} 
			if (stuckString == "unstuck"){ //Ground control software says to try again
				x.attach(XSERVO); //reattach the motors
				y.attach(YSERVO);
				z.attach(ZSERVO);
				Serial.println("trying again");
				break;
			}
		}
	}
}

/*The Move() function moves the tool in a straight line to the position (xEnd, yEnd, zEnd) (mm) at the speed moveSpeed (mm/min). Movements are correlated so that regardless of the distances moved in each direction, the tool moves to the target in a straight line. This function is used by the G0 and G1 commands.*/
int Move(float xEnd, float yEnd, float zEnd, float moveSpeed, int g0){
        xEnd = xEnd * XunitScalar; \\Work in rotations internally.
        yEnd = yEnd * YunitScalar;
        zEnd = zEnd * ZunitScalar;
	float curXtarget, curYtarget, curZtarget;
	long mtime = millis();
	long ntime = millis();
	float xIncmtDist, yIncmtDist, zIncmtDist;
	float pathLength = sqrt(sq(location.xtarget - xEnd) + sq(location.ytarget - yEnd) + sq(location.ztarget - zEnd));
	float sinX = 0;
	float sinY = 0;
	float sinZ = 0;
	float speed;

	float tempXpos = location.xpos;
	float tempYpos = location.ypos;
	float tempZpos = location.zpos;
	float deltaX = 5;
	float deltaY = 5;
	float deltaZ = 5;

	static int shortCount = 0;
	static int gain = KPP;
	int timeStep = TIMESTEP;
	//Serial.println("Length: ");
	//Serial.println(pathLength);
		
	/* With luck, this won't be necessary with PID. */
	/* if(pathLength < 1){ //This is used to adjust the machine's behavior when cutting many small lines */
	/* 	shortCount++; */
	/* } */
	/* if(pathLength > 3){ */
	/* 	shortCount = 0; */
	/* } */
	
	/* if(shortCount > 4){ */
	/* 	gain = KP; */
	/* } */
	/* else{ */
	/*   gain = KP;//KPP; */
	/* } */
	
	if(pathLength < .1){ //This prevents div/0 errors.
		pathLength = .1;
	}
	
	sinX = (location.xtarget - xEnd)/pathLength;
	sinY = (location.ytarget - yEnd)/pathLength;
	sinZ = (location.ztarget - zEnd)/pathLength;

	speed = (g0?MAXSPEED;MAXCUTSPEED);
	if ( moveSpeed * XunitScalar * sinX > speed){ //Limits the movement speed to the ability of the machine.
		  moveSpeed = MAXSPEED / (XunitScalar * sinX);
	}
	if ( moveSpeed * YunitScalar * sinY > speed){
		  moveSpeed = MAXSPEED / (YunitScalar * sinY);
	}
	if ( moveSpeed * ZunitScalar * sinZ > speed){
		  moveSpeed = MAXSPEED / (ZunitScalar * sinZ);
	}

        xIncmtDist = (timeStep * moveSpeed * XunitScalar * sinX) / 60000; //Sets up the distance to be moved at each tick. Convert ms to minutes.
        if(location.xtarget > xEnd){
		xIncmtDist = -xIncmtDist;
	}
	yIncmtDist = (timeStep * moveSpeed * YunitScalar * sinY) / 60000;
        if(location.xtarget > xEnd){
		xIncmtDist = -xIncmtDist;
	}
	zIncmtDist = (timeStep * moveSpeed * ZunitScalar * sinZ) / 60000;
        if(location.xtarget > xEnd){
		xIncmtDist = -xIncmtDist;
	}

	String stopString = "";
	while(1){ //The movement takes place in here
		SetPos(&location);
		SetTarget(location.xtarget, location.ytarget, location.ztarget, &location);
		if( millis() - mtime > timeStep){ 
			if(abs(location.xpos - location.xtarget) < MOVETOLERANCE && abs(location.ypos - location.ytarget) < MOVETOLERANCE && abs(location.zpos - location.ztarget) < MOVETOLERANCE){ //updates the position if the tool is close to the target and the elapsed time has passed
				location.xtarget = location.xtarget + xIncmtDist;
				location.ytarget = location.ytarget + yIncmtDist;
				location.ztarget = location.ztarget + zIncmtDist;
				mtime = millis();
			}
		}
		//Serial.println(abs(location.ypos - location.ytarget));
		if( millis() - ntime > 300){ //Checks to see if the machine is stuck
			deltaX = abs(tempXpos - location.xpos); //where it was - where it is now = the change
			deltaY = abs(tempYpos - location.ypos);
			deltaZ = abs(tempZpos - location.zpos);
			
			tempXpos = location.xpos;
			tempYpos = location.ypos;
			tempZpos = location.zpos;
			
			if(abs(location.xpos - xEnd) > .2 && deltaX < .01 && abs(location.xpos - location.xtarget) > .1){//The machine has not moved significantly in the last 300ms
				Serial.println("x stuck");
				if(location.xpos < xEnd){
					Unstick(x, -1);
				}
				else{
					Unstick(x, 1);
				}
			}
			if(abs(location.ypos - yEnd) > .2 && deltaY < .01 && abs(location.ypos - location.ytarget) > .1){
				Serial.println("y stuck");
				if(location.ypos < yEnd){
					Unstick(y, -1);
				}
				else{
					Unstick(y, 1);
				}
			}
			if(abs(location.zpos - zEnd) > .2 && deltaZ < .01 && abs(location.zpos - location.ztarget) > .1){
				Serial.println("z stuck");
				if(location.zpos < zEnd){
					Unstick(z, -1);
				}
				else{
					Unstick(z, 1);
				}
			}
			
			ntime = millis();
		}
		
		if (Serial.available() > 0) {
			char c = Serial.read();  //gets one byte from serial buffer
			stopString += c; //makes the string readString
			//Serial.println(stopString);
			if(stopString == "STOP"){
				Serial.println("Clear Buffer");
				return(0);
			}
			if(stopString [0] != 'S'){
				stopString = "";
			}
		} 
		
		if(abs(location.xtarget - xEnd) < .1){
			location.xtarget = xEnd;
		}
		if(abs(location.ytarget - yEnd) < .1){
			location.ytarget = yEnd;
		}
		if(abs(location.ztarget - zEnd) < .1){
			location.ztarget = zEnd;
			//Serial.println("hit end");
		}
		
		if(abs(location.xpos - xEnd) < TOLERANCE && abs(location.ypos - yEnd) < TOLERANCE && abs(location.zpos - zEnd) < TOLERANCE){//The machine has completed it's move successfully
			return(1);
		}
		
	}
	return(1);
}

/*G1() is the function which is called to process the string if it begins with 'G01'*/
int G1(String readString){

	float xgoto = 99999; //These are initialized to ridiculous values as a method to check if they have been changed or not, there is a better way to do this?
	float ygoto = 99999;
	float zgoto = 99999;
	float gospeed = 0;
	int i = 0;
	int j = 0;
	int begin;
	int end;
	char sect[22]; //This whole section is kinda a mess maybe it should be done like g1go()?
	while (j < 23){
		sect[j] = ' ';
		j++;
	}
	i = 2;
	memset(sect,0,sizeof(sect)); //This clears the array
	while (i <= readString.length()){ //This code extracts the relevant information from the string. It's crude and should be made to work like the code in the g2go() function.
		if(readString[i] == 'X'){
			begin = i + 1;
			while (i <= readString.length()){
				if(readString[i] == ' '){
					end = i - 1;
					break;
				}
				i++;
			}
			i = begin;
			while(i <= end){
				sect[i - begin] = readString[i];
				i++;
			}
			xgoto = atof(sect);
			xgoto = -xgoto;
			break;
		}
		i++;
	}
	i = 2;
	memset(sect,0,sizeof(sect)); //This clears the array
	while (i <= readString.length()){
		//Serial.println("in length ran");
		j = 0;
		while (j < 23){
			sect[j] = ' ';
			j++;
		}
		if(readString[i] == 'Y'){
			//Serial.println("ydetected at");
			//Serial.println(i);
			begin = i + 1;
			while (i <= readString.length()){
				//Serial.println("that");
				if(readString[i] == ' '){
					//Serial.println("space detected at");
					//Serial.println(i);
					end = i - 1;
					break;
				}
				i++;
			}
			i = begin;
			while(i <= end){
				sect[i - begin] = readString[i];
				i++;
			}
			//Serial.println("Y section:");
			//Serial.println(sect);
			ygoto = atof(sect);
			break;
		}
		i++;
	}
	i = 2;
	memset(sect,0,sizeof(sect)); //This clears the array
	while (i <= readString.length()){
		j = 0;
		while (j < 23){
			sect[j] = ' ';
			j++;
		}
		//Serial.println("in length ran");
		if(readString[i] == 'Z'){
			//Serial.println("zdetected at");
			//Serial.println(i);
			begin = i + 1;
			while (i <= readString.length()){
				//Serial.println("that");
				if(readString[i] == ' '){
					//Serial.println("space detected at");
					//Serial.println(i);
					end = i - 1;
					break;
				}
				i++;
			}
			i = begin;
			while(i <= end){
				sect[i - begin] = readString[i];
				i++;
			}
			zgoto = atof(sect);
			break;
		}
		i++;
	}
	i = 2;
	memset(sect,0,sizeof(sect)); //This clears the array
	while (i <= readString.length()){
		//Serial.println("in length ran");
		if(readString[i] == 'F'){
			//Serial.println("In G01, fdetected at");
			//Serial.println(i);
			begin = i + 1;
			while (i <= readString.length()){
				if(readString[i] == ' '){
					//Serial.println("space detected at");
					//Serial.println(i);
					end = i - 1;
					break;
				}
				i++;
			}
			i = begin;
			while(i <= end){
				sect[i - begin] = readString[i];
				i++;
			}
			//Serial.println("Sect: ");
			//Serial.println(sect);
			gospeed = atof(sect);
			break;
		}
		i++;
	}
	
	
	if(XunitScalar > (1 / XPITCH)){ //running in inches
			gospeed = gospeed * 25.4; //convert from inches, now mm/min
	}
	feedrate = gospeed; //mm per min
	
	xgoto = xgoto * XunitScalar;
	ygoto = ygoto * YunitScalar;
	zgoto = zgoto * ZunitScalar;
	
	
	
	if( xgoto > 9000 ){ //These check to see if a variable hasn't been changed and make the machine hold position on that axis
		xgoto = location.xtarget;
	}
	if( ygoto > 9000 ){
		ygoto = location.ytarget;
	}
	if( zgoto > 9000 ){
		zgoto = location.ztarget;
	}
	
	
	@	int tempo = Move(xgoto, ygoto, zgoto, feedrate, 0); //The move is performed
	
	if (tempo == 1){ //If the move finishes successfully
	        location.xtarget = xgoto * XunitScalar;
	        location.ytarget = ygoto * YunitScalar;
	        location.ztarget = zgoto * ZunitScalar;
	}
}

/*G0() is the function which is called to process the string if it begins with 'G00'*/
int G0(String readString){

	float xgoto = 99999; //These are initialized to ridiculous values as a method to check if they have been changed or not, there is a better way to do this?
	float ygoto = 99999;
	float zgoto = 99999;
	int i = 0;
	int j = 0;
	int begin;
	int end;
	char sect[22]; //This whole section is kinda a mess maybe it should be done like g1go()?
	while (j < 23){
		sect[j] = ' ';
		j++;
	}
	i = 2;
	memset(sect,0,sizeof(sect)); //This clears the array
	while (i <= readString.length()){ //This code extracts the relevant information from the string. It's crude and should be made to work like the code in the g2go() function.
		if(readString[i] == 'X'){
			begin = i + 1;
			while (i <= readString.length()){
				if(readString[i] == ' '){
					end = i - 1;
					break;
				}
				i++;
			}
			i = begin;
			while(i <= end){
				sect[i - begin] = readString[i];
				i++;
			}
			xgoto = atof(sect);
			xgoto = -xgoto;
			break;
		}
		i++;
	}
	i = 2;
	memset(sect,0,sizeof(sect)); //This clears the array
	while (i <= readString.length()){
		//Serial.println("in length ran");
		j = 0;
		while (j < 23){
			sect[j] = ' ';
			j++;
		}
		if(readString[i] == 'Y'){
			//Serial.println("ydetected at");
			//Serial.println(i);
			begin = i + 1;
			while (i <= readString.length()){
				//Serial.println("that");
				if(readString[i] == ' '){
					//Serial.println("space detected at");
					//Serial.println(i);
					end = i - 1;
					break;
				}
				i++;
			}
			i = begin;
			while(i <= end){
				sect[i - begin] = readString[i];
				i++;
			}
			//Serial.println("Y section:");
			//Serial.println(sect);
			ygoto = atof(sect);
			break;
		}
		i++;
	}
	i = 2;
	memset(sect,0,sizeof(sect)); //This clears the array
	while (i <= readString.length()){
		j = 0;
		while (j < 23){
			sect[j] = ' ';
			j++;
		}
		//Serial.println("in length ran");
		if(readString[i] == 'Z'){
			//Serial.println("zdetected at");
			//Serial.println(i);
			begin = i + 1;
			while (i <= readString.length()){
				//Serial.println("that");
				if(readString[i] == ' '){
					//Serial.println("space detected at");
					//Serial.println(i);
					end = i - 1;
					break;
				}
				i++;
			}
			i = begin;
			while(i <= end){
				sect[i - begin] = readString[i];
				i++;
			}
			zgoto = atof(sect);
			break;
		}
		i++;
	}
	i = 2;
	memset(sect,0,sizeof(sect)); //This clears the array
	while (i <= readString.length()){
		//Serial.println("in length ran");
		if(readString[i] == 'F'){
			//Serial.println("In G00, fdetected at");
			//Serial.println(i);
			begin = i + 1;
			while (i <= readString.length()){
				if(readString[i] == ' '){
					//Serial.println("space detected at");
					//Serial.println(i);
					end = i - 1;
					break;
				}
				i++;
			}
			i = begin;
			while(i <= end){
				sect[i - begin] = readString[i];
				i++;
			}
			//Serial.println("Sect: ");
			//Serial.println(sect);
			// F in G00 is an error. Ignore it.
			break;
		}
		i++;
	}
	
	xgoto = xgoto * XunitScalar;
	ygoto = ygoto * YunitScalar;
	zgoto = zgoto * ZunitScalar;
	
	
	
	if( xgoto > 9000 ){ //These check to see if a variable hasn't been changed and make the machine hold position on that axis
		xgoto = location.xtarget;
	}
	if( ygoto > 9000 ){
		ygoto = location.ytarget;
	}
	if( zgoto > 9000 ){
		zgoto = location.ztarget;
	}
	
	
		int tempo = Move(xgoto, ygoto, zgoto, 99999, 1); //The move is performed
	
	if (tempo == 1){ //If the move finishes successfully
	        location.xtarget = xgoto * XunitScalar;
	        location.ytarget = ygoto * YunitScalar;
	        location.ztarget = zgoto * ZunitScalar;
	}
}

/*Circle two takes in the radius of the circle to be cut and the starting and ending points in radians with pi removed so a complete circle is from 0 to 2. If direction is 1 the function cuts a CCW circle, and -1 cuts a CW circle. The direction that one moves from zero changes between the two directions, meaning that a quarter circle is always given by 0,.5 regardless of the direction. So direction = -1 start = 0 end = .5 makes a 1/4 circle downward and direction = 1 start = 0 end = .5 makes a 1/4 circle upward starting from the right side of the circle*/
int Circle(float radius, int direction, float xcenter, float ycenter, float startrad, float endrad, float speed){
	int i = 0;
	int j = 0;
	int comp = 1;
	int endAngle = 0;
	float xdist, ydist;
	float origxloc = -1 * location.xtarget;
	float origyloc = location.ytarget;
	float origxAngleOffset, origyAngleOffset;
	long stime = millis();
	long ntime = millis();
	float deltaX = 5;
	float deltaY = 5;
	float deltaZ = 5;
	float tempXpos = location.xpos;
	float tempYpos = location.ypos;
	float tempZpos = location.zpos;
	float stepMultiplier = 55.0; //prevents the circle from looking like its made up of many small lines. 55 is just a made up number that seems about right.
	float timeStep = -1.45*speed + 200;
	
	/*Serial.println("Rads: ");
	Serial.println(startrad);
	Serial.println(endrad);*/
	
	//This addresses a weird issue where sometimes CAD packages use a circle with a HUGE radius to aproximate a straight line. The problem with this is that the arduino has
	//a hard time doing very precise floating point math, so when you use a very large radius it ends up being inaccurate. This should be solved in some better way.
	if(radius > 300 && abs(startrad - endrad) < 0.2){ 
		return(1);
	}

	endAngle = endrad*stepMultiplier*radius;
	i = startrad*stepMultiplier*radius;
	
	if(endrad < startrad){ //avoids weird behavior when not valid indices are sent
		endAngle = endAngle + (int)(2*stepMultiplier*radius);
	}
	
	/*Serial.println("IN CIRCLE: ");
	Serial.println(radius);
	Serial.println(direction);
	Serial.println(startrad,7);
	Serial.println(endrad,7);
	Serial.print("Start i: ");
	Serial.println(i);
	Serial.print("End i: ");
	Serial.println(endAngle);*/
	
	String stopString = "";
	while(i<endAngle){ //Actual movement takes place by incrementing the target position along the circle.
		if (Serial.available() > 0) {
			char c = Serial.read();  //gets one byte from serial buffer
			stopString += c; //makes the string readString
			//Serial.println(stopString);
			if(stopString == "STOP"){
				Serial.println("STOP");
				Serial.println("Clear Buffer");
				return(0);
			}
			if(stopString [0] != 'S'){
				stopString = "";
			}
		} 
		
		location.xtarget = -1*radius * cos(3.141593*((float)i/(int)(stepMultiplier*radius))) - xcenter; //computes the new target position.
		location.ytarget = direction * radius * sin(3.141593*((float)i/(int)(stepMultiplier*radius))) + ycenter;
		
		SetPos(&location);
		SetTarget(location.xtarget, location.ytarget, location.ztarget, &location, KPP);
		
		if( millis() - stime > timeStep ){
			if( abs(location.xpos - location.xtarget) < TOLERANCE && abs(location.ypos - location.ytarget) < TOLERANCE && abs(location.zpos - location.ztarget) < TOLERANCE){ //if the target is reached move to the next position
				i++;
				stime = millis();
			}
		}
		
		if( millis() - ntime > 300){
			deltaX = abs(tempXpos - location.xpos); 
			deltaY = abs(tempYpos - location.ypos);
			deltaZ = abs(tempZpos - location.zpos);
			
			tempXpos = location.xpos;
			tempYpos = location.ypos;
			tempZpos = location.zpos;
			
			if(deltaX < .01 && abs(location.xpos - location.xtarget) > .1){
				//Serial.println("x stuck");
				if(location.xpos < location.xtarget){
					Unstick(x, -1);
				}
				else{
					Unstick(x, 1);
				}
			}
			if(deltaY < .01 && abs(location.ypos - location.ytarget) > .1){
				//Serial.println("y stuck");
				if(location.ypos < location.ytarget){
					Unstick(y, -1);
				}
				else{
					Unstick(y, 1);
				}
			}
			if(deltaZ < .01 && abs(location.zpos - location.ztarget) > .1){
				//Serial.println("z stuck");
				if(location.zpos < location.ztarget){
					Unstick(z, -1);
				}
				else{
					Unstick(z, 1);
				}
			}
			
			ntime = millis();
		}
	}
	//Serial.print("End i: ");
	//Serial.println(i);
	return(1);
}

/*G2() is the function which is called when the string sent to the machine is 'G02' or 'G03'. The string is parsed to extract the relevant information which is then used to compute the start and end points of the circle and the the circle() function is called.*/
int G2(String readString){
	int rpos;
	int mpos;
	int npos;
	int xpos;
	int ypos;
	int ipos;
	int jpos;
	int fpos;
	int rspace;
	int mspace;
	int nspace;
	int xspace;
	int yspace;
	int ispace;
	int jspace;
	int fspace;
	
	float radius = 0, mval = 0, nval = 0, xval = 0, yval = 0, ival = 0, jval = 0, fval = 0;
	char rsect[] = "                        ";
	char msect[] = "                        ";
	char nsect[] = "                        ";
	char xsect[] = "                        ";
	char ysect[] = "                        ";
	char isect[] = "                        ";
	char jsect[] = "                        ";
	char fsect[] = "                        ";
	
	rpos = readString.indexOf('R');
	mpos = readString.indexOf('M');
	npos = readString.indexOf('N');
	xpos = readString.indexOf('X');
	ypos = readString.indexOf('Y');
	ipos = readString.indexOf('I');
	jpos = readString.indexOf('J');
	fpos = readString.indexOf('F');
	
	rspace = readString.indexOf(' ', rpos);
	mspace = readString.indexOf(' ', mpos);
	nspace = readString.indexOf(' ', npos);
	xspace = readString.indexOf(' ', xpos);
	yspace = readString.indexOf(' ', ypos);
	ispace = readString.indexOf(' ', ipos);
	jspace = readString.indexOf(' ', jpos);
	fspace = readString.indexOf(' ', fpos);
	
	readString.substring((rpos + 1), rspace).toCharArray(rsect, 23);
	readString.substring((mpos + 1), mspace).toCharArray(msect, 23);
	readString.substring((npos + 1), nspace).toCharArray(nsect, 23);
	readString.substring((xpos + 1), xspace).toCharArray(xsect, 23);
	readString.substring((ypos + 1), yspace).toCharArray(ysect, 23);
	readString.substring((ipos + 1), ispace).toCharArray(isect, 23);
	readString.substring((jpos + 1), jspace).toCharArray(jsect, 23);
	readString.substring((fpos + 1), fspace).toCharArray(fsect, 23);
	
	xval = atof(xsect)*XunitScalar;//The relevant information has been extracted
	yval = atof(ysect)*YunitScalar;
	ival = atof(isect)*XunitScalar;
	jval = atof(jsect)*YunitScalar;
	fval = atof(fsect);

	if (xpos == -1){ //If x is not found in the provided string
		xval = -location.xtarget; //The xval is the current location of the machine
	}

	if (ypos == -1){ //If y is not found in the provided string
		yval = location.ytarget; //The yval is the current location of the machine
	}
	
	if(XunitScalar > (1 / XPITCH)){ //running in inches
			fval = fval * 25.4; //convert from inches
	}
	
	if(fval > 4){ //preserves the feedrate for the next call
		feedrate = fval;
	}
	
	float ScaledXLoc = -1*location.xtarget;
	float ScaledYLoc = location.ytarget;
	float xCenter = ScaledXLoc + ival;
	float yCenter = ScaledYLoc + jval;
	
	if (xval != 0 || yval != 0 || ival != 0 || jval != 0){ //if some valid data is present
		radius =  sqrt(sq(ival) + sq(jval)); //computes the radius
		mval = getAngle(ScaledXLoc, ScaledYLoc, xCenter, yCenter); //computes the starting point on the circle
		nval = getAngle(xval, yval, xCenter, yCenter); //computes the ending point on the circle
	}
	
	
	int CircleReturnVal = 0;
	if(readString[2] == '2' || readString[1] == '2'){
		mval = 2 - mval; //flips the direction
		nval = 2 - nval;
		CircleReturnVal = Circle(radius, -1, xCenter, yCenter, mval, nval, feedrate);
	}
	else{
		CircleReturnVal = Circle(radius, 1, xCenter, yCenter, mval, nval, feedrate);
	}
	
	
	if(CircleReturnVal == 1){ //If the circle was cut correctly
		while( abs(location.xpos + xval) > TOLERANCE or abs(location.ypos - yval) > TOLERANCE){ //This ensures that the circle is completed and that if it is a circle with a VERY large radius and a small angle it isn't neglected
			SetTarget(-1*xval, yval, location.ztarget, &location, KPP);
			//Serial.println(abs(location.xpos + xval));
			SetPos(&location);
		}
		location.xtarget = -1*xval;
		location.ytarget = yval;
	}
	else{  //If something went wrong while cutting the circle
		location.xtarget = location.xpos;
		location.ytarget = location.ypos;
	}
}

/*The testEncoders() function tests that the encoders are connected and working properly. It does this by measuring the produced pulse width. If there is no pulse width then the encoder is not connected or is exactly at the zero position.*/
int testEncoders(){
	Serial.println("\nTesting Encoders");
	if(PWMread(xpot) == 0){
		Serial.println("\nThe encoder on the xaxis of your machine did not respond. This is most likely due to a bad connection between the microcontroller and the encoder. This may be because the encoder is plugged in backwards.\n");
	}
	else{
		Serial.println("X axis encoder working correctly.");
		Serial.println(PWMread(xpot));
	}
	if(PWMread(ypot) == 0){
		Serial.println("\nThe encoder on the yaxis of your machine did not respond. This is most likely due to a bad connection between the microcontroller and the encoder. This may be because the encoder is plugged in backwards.\n");
	}
	else{
		Serial.println("Y axis encoder working correctly.");
		Serial.println(PWMread(ypot));
	}
	if(PWMread(zpot) == 0){
		Serial.println("\nThe encoder on the zaxis of your machine did not respond. This is most likely due to a bad connection between the microcontroller and the encoder. This may be because the encoder is plugged in backwards.\n");
	}
	else{
		Serial.println("Z axis encoder working correctly.");
		Serial.println(PWMread(zpot));
	}
	return(1);
}

/*The testMotors() function tests that the motors are all connected. It does this by driving each motor forward then backwards for a set amount of time.*/
int testMotors(){
	Serial.println("Testing Motors");
	x.write(90);
	y.write(90);
	z.write(90);
	
	
	x.write(180); //Spin the motor backwards
	long tmptime = millis();
	while(millis() - tmptime < 600){ //This is just a delay which doesn't lose the machine's position.
		SetPos(&location); 
	}
	x.write(0); //Spin the motor forward again
	tmptime = millis();
	while(millis() - tmptime < 600){
		SetPos(&location); 
	}
	x.write(90);
	
	y.write(180); //Spin the motor backwards
	tmptime = millis();
	while(millis() - tmptime < 600){ //This is just a delay which doesn't lose the machine's position.
		SetPos(&location); 
	}
	y.write(0); //Spin the motor forward again
	tmptime = millis();
	while(millis() - tmptime < 600){
		SetPos(&location); 
	}
	y.write(90);
	
	z.write(180); //Spin the motor backwards
	tmptime = millis();
	while(millis() - tmptime < 600){ //This is just a delay which doesn't lose the machine's position.
		SetPos(&location); 
	}
	z.write(0); //Spin the motor forward again
	tmptime = millis();
	while(millis() - tmptime < 600){
		SetPos(&location); 
	}
	z.write(90);
}

/*The testBoth() function checks that the motors and encoders are plugged in correctly. It does this by driving the motors and then measuring that the correct shaft rotates using the encoders.*/
int testBoth(){
	Serial.println("Testing System");
	x.write(90);
	y.write(90);
	z.write(90);
	
	float deltaX = location.xpos;
	float deltaY = location.ypos;
	float deltaZ = location.zpos;
	int problemFlag = 0;
	
	x.write(180); //Spin the motor backwards
	long tmptime = millis();
	while(millis() - tmptime < 600){ //This is just a delay which doesn't lose the machine's position.
		SetPos(&location); 
	}
	x.write(0); //Spin the motor forward again
	
	deltaX = abs(location.xpos - deltaX);
	deltaY = abs(location.ypos - deltaY);
	deltaZ = abs(location.zpos - deltaZ);

	if(deltaX < .25){
		Serial.println("x motor problem");
		problemFlag = 1;
	}
	if (deltaX < deltaY and deltaY > .25){
		Serial.println("Swap with Y1");
		problemFlag = 1;
	}
	if (deltaX < deltaZ and deltaZ > .25){
		Serial.println("Swap with Z1");
		problemFlag = 1;
	}
	
	tmptime = millis();
	while(millis() - tmptime < 600){
		SetPos(&location); 
	}
	x.write(90);
	
	deltaX = location.xpos;
	deltaY = location.ypos;
	deltaZ = location.zpos;
	y.write(180); //Spin the motor backwards
	tmptime = millis();
	while(millis() - tmptime < 600){ //This is just a delay which doesn't lose the machine's position.
		SetPos(&location); 
	}
	
	deltaX = abs(location.xpos - deltaX);
	deltaY = abs(location.ypos - deltaY);
	deltaZ = abs(location.zpos - deltaZ);

	if(deltaY < .25){
		Serial.println("y motor problem");
		problemFlag = 1;
	}
	if (deltaY < deltaX and deltaX > .25){
		Serial.println("Swap with X2");
		Serial.println(deltaX);
		Serial.println(deltaY);
		problemFlag = 1;
	}
	if (deltaY < deltaZ and deltaZ > .25){
		Serial.println("Swap with Z2");
		problemFlag = 1;
	}
	
	y.write(0); //Spin the motor forward again
	tmptime = millis();
	while(millis() - tmptime < 600){
		SetPos(&location); 
	}
	y.write(90);
	
	deltaX = location.xpos;
	deltaY = location.ypos;
	deltaZ = location.zpos;
	
	z.write(180); //Spin the motor backwards
	tmptime = millis();
	while(millis() - tmptime < 600){ //This is just a delay which doesn't lose the machine's position.
		SetPos(&location); 
	}
	deltaX = abs(location.xpos - deltaX);
	deltaY = abs(location.ypos - deltaY);
	deltaZ = abs(location.zpos - deltaZ);

	if(deltaZ < .25){
		Serial.println("Z motor problem");
		problemFlag = 1;
	}
	if (deltaZ < deltaY and deltaY > .25){
		Serial.println("Swap with Y3");
		problemFlag = 1;
	}
	if (deltaZ < deltaX and deltaX > .25){
		Serial.println("Swap with X3");
		problemFlag = 1;
	}
	
	z.write(0); //Spin the motor forward again
	tmptime = millis();
	while(millis() - tmptime < 600){
		SetPos(&location); 
	}
	z.write(90);
	
	if(problemFlag == 0){
		Serial.println("All tests passed");
		return(1);
	}
	else{
		return(0);
	}
}

void centerMotors(){
	x.write(90);
	y.write(90);
	z.write(90);
	long tmptime = millis();
	while(millis() - tmptime < 2000){ //This is just a delay which doesn't lose the machine's position.
		SetPos(&location); 
	}
}

int calibrateMagnets(){
	Serial.println("Calibrating Magnets");
	
	x.write(90);
	y.write(90);
	z.write(90);
	
	int maxXVal = 0;
	int maxYVal = 0;
	int maxZVal = 0;
	
	int tempVal = 0;
	int i = 0;
	x.write(180);
	y.write(90);
	z.write(90);
	while(i<2000){
		tempVal = pulseIn(xpot, HIGH, 2000); 
		if (tempVal > maxXVal){
			maxXVal = tempVal;
		}
		i++;
	}
	
	tempVal = 0;
	i = 0;
	x.write(0);
	y.write(90);
	z.write(90);
	while(i<2000){
		tempVal = pulseIn(xpot, HIGH, 2000); 
		if (tempVal > maxXVal){
			maxXVal = tempVal;
		}
		i++;
	}
	
	
	tempVal = 0;
	i = 0;
	x.write(90);
	y.write(180);
	z.write(90);
	while(i<2000){
		tempVal = pulseIn(ypot, HIGH, 2000); 
		if (tempVal > maxYVal){
			maxYVal = tempVal;
		}
		i++;
	}
	
	tempVal = 0;
	i = 0;
	x.write(90);
	y.write(0);
	z.write(90);
	while(i<2000){
		tempVal = pulseIn(ypot, HIGH, 2000); 
		if (tempVal > maxYVal){
			maxYVal = tempVal;
		}
		i++;
	}
	
	tempVal = 0;
	i = 0;
	x.write(90);
	y.write(90);
	z.write(180);
	while(i<2000){
		tempVal = pulseIn(zpot, HIGH, 2000); 
		if (tempVal > maxZVal){
			maxZVal = tempVal;
		}
		i++;
	}
	
	tempVal = 0;
	i = 0;
	x.write(90);
	y.write(90);
	z.write(0);
	while(i<2000){
		tempVal = pulseIn(zpot, HIGH, 2000); 
		//Serial.println(tempVal);
		if (tempVal > maxZVal){
			maxZVal = tempVal;
		}
		i++;
	
	}
	//maxXVal*x = 1024
	//x = 1024/maxXVal
	/*Serial.println(maxXVal);
	Serial.println(maxYVal);
	Serial.println(maxZVal);*/
	
	int xMagScale = round(100*(1024.0/float(maxXVal)));
	int yMagScale = round(100*(1024.0/float(maxYVal)));
	int zMagScale = round(100*(1024.0/float(maxZVal)));
	
	if (xMagScale < 60 || yMagScale < 60 || zMagScale < 60 ){	
		Serial.println("Magnet calibration failed. Please try again.");
		return 0;
	}
	Serial.println(xMagScale);
	Serial.println(yMagScale);
	Serial.println(zMagScale);
	EEPROM.write(1,xMagScale);
	EEPROM.write(2,yMagScale);
	EEPROM.write(3,zMagScale);
	EEPROM.write(4,56);//This is a marker value which is used to check if valid data can be read later
	Serial.println("Magnet Positions Calibrated");
	return 1;
}
	
/*The G10() function handles the G10 gcode which re-zeroes one or all of the machine's axes.*/
void G10(String readString){
	
	if(readString.indexOf('X') > 2){
		location.xpos = 0.0;
		location.xtarget = 0.0;
	}
	if(readString.indexOf('Y') > 2){
		location.ypos = 0.0;
		location.ytarget = 0.0;
	}
	if(readString.indexOf('Z') > 2){
		location.zpos = 0.0;
		location.ztarget = 0.0;
	}
}

int ManualControl(String readString){
	String readString2 = readString;
	int stringLength = readString2.length();
	long tmptime = millis();
	while(1){
		if (Serial.available()){
			while (Serial.available()) {
				delay(1);  //delay to allow buffer to fill 
				if (Serial.available() > 0) {
					char c = Serial.read();  //gets one byte from serial buffer
					readString2 += c; //makes the string readString
				} 
			}
		}
		SetPos(&location);
		stringLength = readString2.length();
		if(stringLength > 0){
			Serial.println(readString2);
			
			if(readString2 == "Exit Manual Control "){
				Serial.println("Test Complete");
				return(1);
			}
			if(readString2.indexOf('X') > 2 && readString2.indexOf('B') == 0){
				x.write((readString2.substring(5)).toInt());
				tmptime = millis();
			}
			if(readString2.indexOf('Y') > 2 && readString2.indexOf('B') == 0){
				y.write((readString2.substring(5)).toInt());
				tmptime = millis();
			}
			if(readString2.indexOf('Z') > 2 && readString2.indexOf('B') == 0){
				z.write((readString2.substring(5)).toInt());
				tmptime = millis();
			}
			
			Serial.println("gready");
		}
		if(millis() - tmptime > 5000){
			Serial.println("Test Complete - Timed Out");
			return(1);
		}
		
		readString2 = "";
	}
}

float toolOffset(int pin){
	long tmptime = millis();
	long tmpTimeout = millis();
	while(1){
		tmptime = millis();
		location.ztarget = location.ztarget - .05;
		while(millis() - tmptime < 100){ //This is just a delay which doesn't lose the machine's position.
			SetPos(&location); 
			SetTarget(location.xtarget, location.ytarget, location.ztarget, &location, KPP);

			if(digitalRead(pin) == 0){  //The surface has been found
				return(location.zpos);
			}
		}
		if(millis() - tmpTimeout > 3000){ //if it has to move down for more than three seconds it will time out
			Serial.println("Surface not found, position the tool closer to the surface and try again.");
			return(location.zpos);
		}
	}
}

//readFloat and writeFloat functions courtesy of http://www.alexenglish.info/2014/05/saving-floats-longs-ints-eeprom-arduino-using-unions/
float readFloat(unsigned int addr){
	union{
		byte b[4];
		float f;
	} data;
	for(int i = 0; i < 4; i++)
	{
		data.b[i] = EEPROM.read(addr+i);
	}
	return data.f;
}

void writeFloat(unsigned int addr, float x){
	union{
		byte b[4];
		float f;
	} data;
	data.f = x;
	for(int i = 0; i < 4; i++){
		EEPROM.write(addr+i, data.b[i]);
	}
}
