/*
 * two_phase_commit.h
 *
 *  Created on: Dec 5, 2015
 *      Author: abhijeet
 */

#ifndef TWO_PHASE_COMMIT_H_
#define TWO_PHASE_COMMIT_H_


typedef enum state
{
	NOTHING,
	INIT,
	READY,
	WAIT,
	ABORT,
	COMMIT,
}state;

#endif /* TWO_PHASE_COMMIT_H_ */
