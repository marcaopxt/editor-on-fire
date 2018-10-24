#include "beat.h"
#include "main.h"
#include "midi.h"
#include "modules/g-idle.h"
#include "note.h"
#include "pathing.h"
#include "song.h"
#include "undo.h"
#include "utility.h"

#ifdef USEMEMWATCH
#include "memwatch.h"
#endif

int eof_note_is_last_longest_gem(EOF_SONG *sp, unsigned long track, unsigned long note)
{
	unsigned long notepos, index, longestnote;
	long prevnote, nextnote, thislength, longestlength;

	if(!sp || (track >= sp->tracks) || (note >= eof_get_track_size(sp, track)))
		return 0;	//Invalid parameters
	if(!(eof_get_note_eflags(sp, track, note) & EOF_NOTE_EFLAG_DISJOINTED))
		return 1;	//If the note doesn't have disjointed status, it's the longest note at this track difficulty and position

	notepos = eof_get_note_pos(sp, track, note);
	index = note;		//Unless found otherwise, the specified note is the first note at its position
	longestnote = note;	//And it will be considered the longest note at this position
	longestlength = eof_get_note_length(sp, track, note);
	while(1)
	{	//Find the first note in this track difficulty at this note's position
		prevnote = eof_track_fixup_previous_note(sp, track, index);

		if(prevnote < 0)
			break;	//No earlier notes
		if(eof_get_note_pos(sp, track, prevnote) != notepos)
			break;	//No earlier notes at the same position

		index = prevnote;	//Track the earliest note at this position
	}

	while(1)
	{	//Compare length among all notes at this position
		thislength = eof_get_note_length(sp, track, index);
		if(thislength >= longestlength)
		{	//If this note is at least as long as the longest one encountered so far
			longestnote = index;
			longestlength = thislength;
		}

		nextnote = eof_track_fixup_next_note(sp, track, index);

		if(nextnote < 0)
			break;	//No later notes
		if(eof_get_note_pos(sp, track, nextnote) != notepos)
			break;	//No later notes at the same position
	}

	if(longestnote == note)	//If no notes at this note's position were longer
		return 1;			//Return true

	return 0;	//Return false
}

unsigned long eof_note_count_gems_extending_to_pos(EOF_SONG *sp, unsigned long track, unsigned long note, unsigned long pos)
{
	unsigned long notepos, index, count = 0, targetlength;
	long prevnote, nextnote;

	if(!sp || (track >= sp->tracks) || (note >= eof_get_track_size(sp, track)))
		return 0;	//Invalid parameters

	notepos = eof_get_note_pos(sp, track, note);
	if(notepos > pos)
		return 0;	//Target position is before that of specified note, there will be no matches

	targetlength = pos - notepos;	//This will be the minimum length of notes to be a match
	index = note;		//Unless found otherwise, the specified note is the first note at its position
	while(1)
	{	//Find the first note in this track difficulty at this note's position
		prevnote = eof_track_fixup_previous_note(sp, track, index);

		if(prevnote < 0)
			break;	//No earlier notes
		if(eof_get_note_pos(sp, track, prevnote) != notepos)
			break;	//No earlier notes at the same position

		index = prevnote;	//Track the earliest note at this position
	}

	while(1)
	{	//Compare length among all notes at this position with the target length
		if(eof_get_note_length(sp, track, index) >= targetlength)
		{	//If this note is long enough to meet the input criteria
			count++;
		}

		nextnote = eof_track_fixup_next_note(sp, track, index);

		if(nextnote < 0)
			break;	//No later notes
		if(eof_get_note_pos(sp, track, nextnote) != notepos)
			break;	//No later notes at the same position
	}

	return count;
}

int eof_note_is_last_in_sp_phrase(EOF_SONG *sp, unsigned long track, unsigned long note)
{
	if(eof_get_note_flags(sp, track, note) & EOF_NOTE_FLAG_SP)
	{	//If this note has star power
		long nextnote = eof_track_fixup_next_note(sp, track, note);

		if(nextnote > 0)
		{	//If there's another note in the same track difficulty
			if(!(eof_get_note_flags(sp, track, nextnote) & EOF_NOTE_FLAG_SP))
			{	//If that next note does not have star power
				return 1;
			}
		}
		else
		{	//The note is the last in its track difficulty
			return 1;
		}
	}

	return 0;
}

unsigned long eof_translate_track_diff_note_index(EOF_SONG *sp, unsigned long track, unsigned char diff, unsigned long index)
{
	unsigned long ctr, tracksize, match;

	if(!sp || (track >= sp->tracks))
		return ULONG_MAX;	//Invalid parameters

	tracksize = eof_get_track_size(sp, track);
	for(ctr = 0, match = 0; ctr < tracksize; ctr++)
	{	//For each note in the specified track
		if(eof_get_note_type(sp, track, ctr) == diff)
		{	//If the note is in the target track difficulty
			if(match == index)
			{	//If this is the note being searched for
				return ctr;
			}
			match++;	//Track how many notes in the target difficulty have been parsed
		}
	}

	return ULONG_MAX;	//No such note was found
}

double eof_get_measure_position(unsigned long pos)
{
	unsigned long beat;
	EOF_BEAT_MARKER *bp;
	double measurepos;
	double beatpos;

	if(!eof_song)
		return 0.0;	//Invalid parameters

	if(!eof_beat_stats_cached)
		eof_process_beat_statistics(eof_song, eof_selected_track);

	beat = eof_get_beat(eof_song, pos);
	if(beat >= eof_song->beats)
		return 0.0;	//Error

	bp = eof_song->beat[beat];
	beatpos = ((double) pos - eof_song->beat[beat]->pos) / eof_get_beat_length(eof_song, beat);		//This is the percentage into its beat the specified position is (only compare the note against the beat's integer position, since the note position will have been rounded in either direction)
	measurepos = ((double) bp->beat_within_measure + beatpos) / bp->num_beats_in_measure;			//This is the percentage into its measure the specified position is
	measurepos += bp->measurenum - 1;																//Add the number of complete measures that are before this position (measurenum is numbered beginning with 1)

	return measurepos;
}

unsigned long eof_ch_pathing_find_next_deployable_sp(EOF_SP_PATH_SOLUTION *solution, unsigned long start_index)
{
	unsigned long ctr, index, sp_count, tracksize;
	double sp_sustain = 0.0;	//The amount of star power sustain that has accumulated

	if(!solution || (solution->track >= eof_song->tracks) || (!solution->note_beat_lengths))
		return ULONG_MAX;	//Invalid parameters

	tracksize = eof_get_track_size(eof_song, solution->track);
	for(ctr = 0, index = 0, sp_count = 0; ctr < tracksize; ctr++)
	{	//For each note in the active track
		if(eof_get_note_type(eof_song, eof_selected_track, ctr) == eof_note_type)
		{	//If the note is in the active difficulty
			if(index >= start_index)
			{	//If the specified note index has been reached, track star power accumulation
				//Count the amount of whammied star power
				if(eof_get_note_length(eof_song, solution->track, ctr) > 1)
				{	//If the note has sustain
					if(eof_get_note_flags(eof_song, solution->track, ctr) & EOF_NOTE_FLAG_SP)
					{	//If the note has star power
						sp_sustain += solution->note_beat_lengths[index];	//Count the number of beats of star power that will be whammied for bonus star power
						while(sp_sustain >= 8.0 - 0.0001)
						{	//For every 8 beats' worth (allowing variance for math error) of star power sustain
							sp_sustain -= 8.0;
							sp_count++;	//Convert it to one star power phrase's worth of star power
						}
					}
				}
				//Count star power phrases to find the first available point of star power deployment
				if(eof_get_note_tflags(eof_song, solution->track, ctr) & EOF_NOTE_TFLAG_SP_END)
				{	//If this is the last note in a star power phrase
					sp_count++;	//Keep count
				}

				if(sp_count >= 2)
				{	//If there are now at least two star power phrase's worth of star power
					return index + 1;	//There is enough star power to deploy, which can be done at the next note
				}
			}
			index++;	//Track the number of notes in the target difficulty that have been encountered
		}
	}

	return ULONG_MAX;	//An applicable note was not found
}

unsigned long eof_ch_pathing_find_next_sp_note(EOF_SP_PATH_SOLUTION *solution, unsigned long start_index)
{
	unsigned long ctr, index, tracksize;

	if(!solution || (solution->track >= eof_song->tracks))
		return ULONG_MAX;	//Invalid parameters

	tracksize = eof_get_track_size(eof_song, solution->track);
	for(ctr = 0, index = 0; ctr < tracksize; ctr++)
	{	//For each note in the active track
		if(eof_get_note_type(eof_song, eof_selected_track, ctr) == eof_note_type)
		{	//If the note is in the active difficulty
			if(index >= start_index)
			{	//If the specified note index has been reached
				if(eof_get_note_flags(eof_song, solution->track, ctr) & EOF_NOTE_FLAG_SP)
				{	//If the note has star power
					return index;
				}
			}
			index++;	//Track the number of notes in the target difficulty that have been encountered
		}
	}

	return ULONG_MAX;	//An applicable note was not found
}

int eof_evaluate_ch_sp_path_solution(EOF_SP_PATH_SOLUTION *solution, unsigned long solution_num, int logging)
{
	int sp_deployed = 0;	//Set to nonzero if star power is currently in effect for the note being processed
	unsigned long tracksize, notectr, ctr;
	unsigned long index;	//This will be the note number relative to only the notes in the target track difficulty, used to index into the solution's arrays
	double sp_deployment_start = 0.0;	//Used to track the start position of star power deployment
	double sp_deployment_end = 0.0;	//The measure position at which the current star power deployment will be over (non-inclusive, ie. a note at this position is NOT in the scope of the deployment)
	unsigned long notescore;		//The base score for the current note
	unsigned long notepos, noteflags, tflags, is_solo;
	long notelength;
	int disjointed;			//Set to nonzero if the note being processed has disjointed status, as it requires different handling for star power whammy bonus
	int representative = 0;	//Set to nonzero if the note being processed is the longest and last gem (criteria in that order) in a disjointed chord, and will be examined for star power whammy bonus
	unsigned long base_score, sp_base_score, sustain_score, sp_sustain_score;	//For logging score calculations
	int deploy_sp;

	//Score caching variables
	EOF_SP_PATH_SCORING_STATE score;		//Tracks current scoring information that will be copied to the score cache after each star power deployment ends
	unsigned long cache_number = ULONG_MAX;	//The cached deploy scoring structure selected to resume from in evaluating this solution
	unsigned long deployment_num;			//The instance number of the next star power deployment

	if(!solution || !solution->deployments || !solution->note_measure_positions || !solution->note_beat_lengths || !eof_song || !solution->track || (solution->track >= eof_song->tracks) || (solution->num_deployments > solution->deploy_count))
	{
		(void) snprintf(eof_log_string, sizeof(eof_log_string) - 1, "\t\t*Invalid parameters.  Solution invalid.");
		eof_log_casual(eof_log_string, 1);
		return 1;	//Invalid parameters
	}

	///Look up cached scoring to skip as much calculation as possible
	for(ctr = 0; ctr < solution->num_deployments; ctr++)
	{	//For each star power deployment in this solution
		if(solution->deployments[ctr] == solution->deploy_cache[ctr].note_start)
		{	//If the star power began at the same time as is called for in this solution
			cache_number = ctr;	//Track the latest cache value that is applicable
		}
		else
		{	//The cached deployment does not match
			break;	//This and all subsequent entries will be marked invalid in the for loop below
		}
	}
	for(; ctr < solution->deploy_count; ctr++)
	{	//For each remaining cache entry that wasn't re-used
		solution->deploy_cache[ctr].note_start = ULONG_MAX;	//Mark the entry as invalid
	}

	if(cache_number < solution->num_deployments)
	{	//If a valid cache entry was identified
		memcpy(&score, &solution->deploy_cache[cache_number], sizeof(EOF_SP_PATH_SCORING_STATE));	//Copy it into the scoring state structure

		notectr = score.note_end_native;	//Resume parsing from the first note after that cached data
		index = score.note_end;
		deployment_num = cache_number + 1;	//The next deployment will be one higher in number than this cached one
	}
	else
	{	//Start processing from the beginning of the track
		//Init score state structure
		score.multiplier = 1;
		score.hitcounter = 0;
		score.score = 0;
		score.deployment_notes = 0;
		score.sp_meter = score.sp_meter_t = 0.0;

		//Init solution structure
		solution->score = 0;
		solution->deployment_notes = 0;

		notectr = index = 0;	//Start parsing notes from the first note
		deployment_num = 0;
	}

	///Optionally log the solution's deployment details
	tracksize = eof_get_track_size(eof_song, solution->track);
	if(logging)
	{	//Skip logging overhead if logging isn't enabled
///DEBUG LOGGING
///		unsigned long notenum, min, sec, ms;
		char tempstring[50];
		int cached = 0;

		(void) snprintf(eof_log_string, sizeof(eof_log_string) - 1, "\tSolution #%lu:  Note indexes ", solution_num);

		for(ctr = 0; ctr < solution->num_deployments; ctr++)
		{	//For each deployment in the solution
			int thiscached = 0;

///DEBUG LOGGING
/*			notenum = eof_translate_track_diff_note_index(eof_song, solution->track, solution->diff, solution->deployments[ctr]);	//Find the real note number for this index
			if(notenum < tracksize)
			{	//If that number was found
				ms = eof_get_note_pos(eof_song, solution->track, notenum);	//Determine mm:ss.ms formatting of timestamp
				min = ms / 60000;
				ms -= 60000 * min;
				sec = ms / 1000;
				ms -= 1000 * sec;
				snprintf(tempstring, sizeof(tempstring) - 1, "%s%s%lu (%lu:%lu.%lu)", (ctr ? ", " : ""), (thiscached ? "*" : ""), solution->deployments[ctr], min, sec, ms);
			}
*/

			if((cache_number < solution->num_deployments) && (cache_number >= ctr))
			{	//If this deployment number is being looked up from cached scoring
				cached = 1;
				thiscached = 1;
			}
			snprintf(tempstring, sizeof(tempstring) - 1, "%s%s%lu", (ctr ? ", " : ""), (thiscached ? "*" : ""), solution->deployments[ctr]);
			strncat(eof_log_string, tempstring, sizeof(eof_log_string) - 1);
		}

		if(!solution->num_deployments)
		{	//If there were no star power deployments in this solution
			strncat(eof_log_string, "(none)", sizeof(eof_log_string) - 1);
		}
		eof_log_casual(eof_log_string, 1);

		if(cached)
		{
			(void) snprintf(eof_log_string, sizeof(eof_log_string) - 1, "\t\tUsing cache for deployment at note index #%lu (Hitcount=%lu  Mult.=x%lu  Score=%lu  D.Notes=%lu  SP Meter=%lu%%).  Resuming from note index #%lu", solution->deploy_cache[cache_number].note_start, score.hitcounter, score.multiplier, score.score, score.deployment_notes, (unsigned long)(score.sp_meter * 100.0 + 0.5), index);
			eof_log_casual(eof_log_string, 1);
			if(index > solution->deployments[deployment_num])
			{	//If the solution indicated to deploy at a note that was already surpassed by the cache entry
				if(logging)
				{
					(void) snprintf(eof_log_string, sizeof(eof_log_string) - 1, "\t\t*Cached scoring shows the attempted deployment is not possible.  Solution invalid.");
					eof_log_casual(eof_log_string, 1);
				}
				return 2;	//Solution invalidated by cache
			}
		}
	}

	///Process the notes in the target track difficulty
	for(; notectr < tracksize; notectr++)
	{	//For each note in the track being evaluated
		if(index >= solution->note_count)	//If all expected notes in the target track difficulty have been processed
			break;	//Stop processing notes
		if(eof_get_note_type(eof_song, solution->track, notectr) != solution->diff)
			continue;	//If this note isn't in the target track difficulty, skip it

		base_score = sp_base_score = sustain_score = sp_sustain_score = 0;
		notepos = eof_get_note_pos(eof_song, solution->track, notectr);
		notelength = eof_get_note_length(eof_song, solution->track, notectr);
		noteflags = eof_get_note_flags(eof_song, solution->track, notectr);
		disjointed = eof_get_note_eflags(eof_song, solution->track, notectr) & EOF_NOTE_EFLAG_DISJOINTED;

		if(disjointed)
		{	//If this is a disjointed gem, determine if this will be the gem processed for whammy star power bonus
			representative = eof_note_is_last_longest_gem(eof_song, solution->track, notectr);
		}

		///Update multiplier
		score.hitcounter++;
		if(score.hitcounter == 30)
		{
			if(logging > 1)
			{
				eof_log_casual("\t\t\tMultiplier increased to x4", 1);
			}
			score.multiplier = 4;
		}
		else if(score.hitcounter == 20)
		{
			if(logging > 1)
			{
				eof_log_casual("\t\t\tMultiplier increased to x3", 1);
			}
			score.multiplier = 3;
		}
		else if(score.hitcounter == 10)
		{
			if(logging > 1)
			{
				eof_log_casual("\t\t\tMultiplier increased to x2", 1);
			}
			score.multiplier = 2;
		}

		///Determine whether star power is to be deployed at this note
		deploy_sp = 0;
		for(ctr = 0; ctr < solution->num_deployments; ctr++)
		{
			if(solution->deployments[ctr] == index)
			{
				deploy_sp = 1;
				break;
			}
		}

		///Determine whether star power is in effect for this note
		if(sp_deployed)
		{	//If star power was in deployment during the previous note, determine if it should still be in effect
			unsigned long truncated_note_position, truncated_sp_deployment_end_position;

			//Truncate the note's position and end of deployment position at 3 decimal places for more reliable comparison
			truncated_note_position = solution->note_measure_positions[index] * 1000;
			truncated_sp_deployment_end_position = sp_deployment_end * 1000;
			if(truncated_note_position >= truncated_sp_deployment_end_position)
			{	//If this note is beyond the end of the deployment
				if(logging > 1)
				{
					(void) snprintf(eof_log_string, sizeof(eof_log_string) - 1, "\t\tStar power ended before note #%lu (index #%lu)", notectr, index);
					eof_log_casual(eof_log_string, 1);
				}
				sp_deployed = 0;	//Star power is no longer in effect
				score.note_end = index;		//This note is the first after the star power deployment that was in effect for the previous note
				score.note_end_native = notectr;

				memcpy(&solution->deploy_cache[deployment_num++], &score, sizeof(EOF_SP_PATH_SCORING_STATE));	//Append this scoring data to the cache
			}
			else
			{	//If star power deployment is still in effect
				if(deploy_sp)
				{	//If the solution specifies deploying star power while it's already in effect
					if(logging)
					{
						(void) snprintf(eof_log_string, sizeof(eof_log_string) - 1, "\t\t*Attempted to deploy star power while it is already deployed.  Solution invalid.");
						eof_log_casual(eof_log_string, 1);
					}
					return 3;	//Solution attempting to deploy while star power is in effect
				}
			}
		}
		if(!sp_deployed)
		{	//If star power is not in effect
			if(deploy_sp)
			{	//If the solution specifies deploying star power at this note
				if(score.sp_meter >= 0.50 - 0.0001)
				{	//If the star power meter is at least half full (allow variance for math error)
					sp_deployment_start = solution->note_measure_positions[index];
					sp_deployment_end = sp_deployment_start + (8.0 * score.sp_meter);	//Determine the end timing of the star power (one full meter is 8 measures)
					score.sp_meter = score.sp_meter_t = 0.0;	//The entire star power meter will be used
					sp_deployed = 1;	//Star power is now in effect
					score.note_start = index;	//Track the note index at which this star power deployment began, for caching purposes
					if(logging > 1)
					{
						(void) snprintf(eof_log_string, sizeof(eof_log_string) - 1, "\t\tDeploying star power at note #%lu (index #%lu).  Projected deployment ending is measure %.2f", notectr, index, sp_deployment_end + 1);
						eof_log_casual(eof_log_string, 1);
					}
				}
				else
				{	//There is insufficient star power
					if(logging)
					{
						(void) snprintf(eof_log_string, sizeof(eof_log_string) - 1, "\t\t*Attempted to deploy star power without sufficient star power.  Solution invalid.");
						eof_log_casual(eof_log_string, 1);
					}
					return 4;	//Solution attempting to deploy without sufficient star power
				}
			}
		}

		///Award star power for completing a star power phrase
		tflags = eof_get_note_tflags(eof_song, solution->track, notectr);
		if(tflags & EOF_NOTE_TFLAG_SP_END)
		{	//If this is the last note in a star power phrase
			if(sp_deployed)
			{	//If star power is in effect
				sp_deployment_end += 2.0;	//Extends the end of star power deployment by 2 measures (one fourth of the effect of a full star power meter)
				if(logging > 1)
				{
					(void) snprintf(eof_log_string, sizeof(eof_log_string) - 1, "\t\t\tStar power deployment ending extended two measures (measure %.2f)", sp_deployment_end + 1);
					eof_log_casual(eof_log_string, 1);
				}
			}
			else
			{	//Star power is not in effect
				score.sp_meter += 0.25;	//Award 25% of a star power meter
				score.sp_meter_t += 0.25;
				if(score.sp_meter > 1.0)
					score.sp_meter = 1.0;	//Cap the star power meter at 100%
			}
		}

		///Special case:  Whammying a sustained star power note while star power is deployed (star power level has to be evaluated as each sustain point is awarded)
		if((noteflags & EOF_NOTE_FLAG_SP) && (notelength > 1) && (sp_deployed))
		{	//If this note has star power, it has sustain and star power is deployed
			double step = 1.0 / 25.0;	//Every 1/25 beat of sustain, a point is awarded.  Evaluate star power gain/loss at every such interval
			double remaining_sustain = solution->note_beat_lengths[index];	//The amount of sustain remaining to be scored for this star power note
			double sp_whammy_gain = 1.0 / 32.0 / 25.0;	//The amount of star power gained from whammying a star power sustain (1/32 meter per beat) during one scoring interval (1/25 beat)
			double realpos = notepos;	//Keep track of the realtime position at each 1/25 beat interval of the note

			base_score = eof_note_count_colors(eof_song, solution->track, notectr) * 50;	//The base score for a note is 50 points per gem
			sp_base_score = base_score;		//Star power adds the same number of bonus points
			notescore = base_score + sp_base_score;
			score.deployment_notes++;	//Track how many notes are played during star power deployment
			score.sp_meter = (sp_deployment_end - solution->note_measure_positions[index]) / 8.0;	//Convert remaining star power deployment duration back to star power
			score.sp_meter_t = score.sp_meter;

			while(remaining_sustain > 0)
			{	//While there's still sustain to examine
				unsigned long beat = eof_get_beat(eof_song, realpos);	//The beat in which this 1/25 beat interval resides
				double sp_drain;	//The rate at which star power is consumed during deployment, dependent on the current time signature in effect
				unsigned long disjointed_multiplier = 1;	//In the case of disjointed chords, this will be set to the number of gems applicable for each sustain point scored

				///Double special case:  For disjointed chords, whammy bonus star power is only given for the longest gem, even though each gem gets points for sustain
				if(disjointed && !representative)
				{	//If this note is a single gem in a disjointed chord but it isn't the one that will be examined for whammy star power bonus
					break;	//Don't process sustain scoring for this gem
				}

				if(remaining_sustain < (step / 2.0))
				{	//If there's less than half a point of sustain left, it will be dropped instead of scored
					break;
				}

				//Update the star power meter
				if((beat >= eof_song->beats) || !eof_song->beat[beat]->has_ts)
				{	//If the beat containing this 1/25 beat interval couldn't be identified, or if the beat has no time signature in effect
					(void) snprintf(eof_log_string, sizeof(eof_log_string) - 1, "\t\t*Logic error.  Solution invalid.");
					eof_log_casual(eof_log_string, 1);
					return 5;	//General logic error
				}
				sp_drain = 1.0 / 8.0 / eof_song->beat[beat]->num_beats_in_measure / 25.0;	//Star power drains at a rate of 1/8 per measure of beats, and this is the amount of drain for a 1/25 beat interval
				score.sp_meter = score.sp_meter + sp_whammy_gain - sp_drain;	//Update the star power meter to reflect the amount gained and the amount consumed during this 1/25 beat interval
				if(score.sp_meter > 1.0)
					score.sp_meter = 1.0;	//Cap the star power meter at 100%

				//Score this sustain interval
				///Double special case:  For disjointed chords, since the whammy bonus star power is only being evaluated for the longest gem, and
				/// sustain points are being scored at the same time, such points have to be awarded for each of the applicable gems in the disjointed chord
				if(remaining_sustain < step)
				{	//If there was less than a point's worth of sustain left, it will be scored and then rounded to nearest point
					double floatscore;

					realpos = (double) notepos + notelength - 1.0;	//Set the realtime position to the last millisecond of the note
					if(disjointed)
					{	//If this is the longest, representative gem in a disjointed chord, count how many of the chord's gems extend far enough to receive this partial bonus point
						disjointed_multiplier = eof_note_count_gems_extending_to_pos(eof_song, solution->track, notectr, realpos + 0.5);
					}
					floatscore = remaining_sustain / step;	//This represents the fraction of one point this remaining sustain is worth
					if(score.sp_meter > 0.0)
					{	//If star power is still in effect
						floatscore = (2 * disjointed_multiplier);	//This remainder of sustain score is doubled due to star power bonus
						sp_sustain_score += floatscore + 0.5;		//For logging purposes, consider this a SP sustain point (since a partial sustain point and a partial SP sustain point won't be separately tracked)
					}
					else
					{
						floatscore = disjointed_multiplier;			//This remainder of sustain score does not get a star power bonus
						sustain_score += floatscore + 0.5;			//Track how many non star power sustain points were awarded
					}
					notescore += (unsigned long) (floatscore + 0.5);	//Round this remainder of sustain score to the nearest point
				}
				else
				{	//Normal scoring
					realpos += eof_get_beat_length(eof_song, beat) / 25.0;	//Update the realtime position by 1/25 of the length of the current beat the interval is in
					if(disjointed)
					{	//If this is the longest, representative gem in a disjointed chord, count how many of the chord's gems extend far enough to receive this bonus point
						disjointed_multiplier = eof_note_count_gems_extending_to_pos(eof_song, solution->track, notectr, realpos + 0.5);
					}
					if(score.sp_meter > 0.0)
					{	//If star power is still in effect
						notescore += (2 * disjointed_multiplier);	//This point of sustain score is doubled due to star power bonus
						sustain_score += disjointed_multiplier;		//Track how many non star power sustain points were awarded
						sp_sustain_score += disjointed_multiplier;	//The same number of points are awarded as a star power bonus
					}
					else
					{
						notescore += disjointed_multiplier;			//This point of sustain score does not get a star power bonus
						sustain_score += disjointed_multiplier;		//Track how many non star power sustain points were awarded
					}
				}

				remaining_sustain -= step;	//One interval less of sustain to examine
			}//While there's still sustain to examine
			if(score.sp_meter > 0.0)
			{	//If the star power meter still has star power, calculate the new end of deployment position
				sp_deployment_start = eof_get_measure_position(notepos + notelength);	//The remainder of the deployment starts at this note's end position
				sp_deployment_end = sp_deployment_start + (8.0 * score.sp_meter);	//Determine the end timing of the star power (one full meter is 8 measures)
				score.sp_meter = score.sp_meter_t = 0.0;	//The entire star power meter will be used
			}
			else
			{	//Otherwise the star power deployment has ended
				score.sp_meter = score.sp_meter_t = 0.0;	//The star power meter was depleted
				sp_deployed = 0;
				score.note_end = index + 1;	//The next note will be the first after this ended star power deployment
				score.note_end_native = notectr + 1;

				memcpy(&solution->deploy_cache[deployment_num++], &score, sizeof(EOF_SP_PATH_SCORING_STATE));	//Append this scoring data to the cache
				if(logging > 1)
				{
					(void) snprintf(eof_log_string, sizeof(eof_log_string) - 1, "\t\tStar power ended during note #%lu (index #%lu)", notectr, index);
					eof_log_casual(eof_log_string, 1);
				}
			}
			notescore *= score.multiplier;	//Apply the current score multiplier in effect
		}//If this note has star power, it has sustain and star power is deployed
		else
		{	//Score the note and evaluate whammy star power gain separately
			double sustain = 0.0;	//The score for the portion of the current note's sustain that is subject to star power bonus
			double sustain2 = 0.0;	//The score for the portion of the current note's sustain that occurs after star power deployment ends mid-note (which is not awarded star power bonus)

			///Add any sustained star power note whammy bonus
			if(noteflags & EOF_NOTE_FLAG_SP)
			{	//If this note has star power
				if(notelength > 1)
				{	//If it has sustain
					///Double special case:  For disjointed chords, whammy bonus star power is only given for the longest gem
					if(!disjointed || representative)
					{	//If this is not a disjointed gem, or it is and this is the gem that is to be examined for the purpose of whammy star power bonus
						double sp_bonus = solution->note_beat_lengths[index] / 32.0;	//Award 1/32 of a star power meter per beat of whammied star power sustain

						score.sp_meter += sp_bonus;	//Add the star power bonus
						score.sp_meter_t += sp_bonus;
						if(score.sp_meter > 1.0)
							score.sp_meter = 1.0;	//Cap the star power meter at 100%
					}
				}
			}

			///Calculate the note's score
			base_score = eof_note_count_colors(eof_song, solution->track, notectr) * 50;	//The base score for a note is 50 points per gem
			notescore = base_score;
			if(notelength > 1)
			{	//If this note has any sustain, determine its score and whether any of that sustain score is not subject to SP bonus due to SP deployment ending in the middle of the sustain (score2)
				sustain2 = 25.0 * solution->note_beat_lengths[index];	//The sustain's base score is 25 points per beat

				if(solution->note_beat_lengths[index] >= 1.000 + 0.0001)
				{	//If this note length is one beat or longer (allowing variance for math error), only the portion occurring before the end of star power deployment is awarded SP bonus
					double sustain_end = eof_get_measure_position(notepos + notelength);

					if(sp_deployed && (sustain_end > sp_deployment_end))
					{	//If star power is deployed and this sustain note ends after the ending of the star power deployment
						double note_measure_length = sustain_end - solution->note_measure_positions[index];		//The length of the note in measures
						double sp_portion = (sp_deployment_end - solution->note_measure_positions[index]) / note_measure_length;	//The percentage of the note that is subject to star power

						sustain = sustain2 * sp_portion;	//Determine the portion of the sustain points that are awarded star power bonus
						sustain2 -= sustain;				//Any remainder of the sustain points are not awarded star power bonus
					}
				}
				else
				{	//If this note length is less than a beat long
					sustain = sustain2;	//All of the sustain is awarded star power bonus
					sustain2 = 0;		//None of it will receive regular scoring
				}
				sustain_score = sustain + sustain2 + 0.5;	//Track how many of the points are being awarded for the sustain (rounded to nearest whole number)
				if(sp_deployed)			//If star power is in effect
				{
					sp_sustain_score = sustain;	//Track how many star power bonus points are being awarded for the sustain
					sustain *= 2.0;		//Apply star power bonus to the applicable portion of the sustain
				}
			}
			if(sp_deployed)				//If star power is in effect
			{
				notescore *= 2;					//It doubles the note's score
				sp_base_score = base_score;
				score.deployment_notes++;	//Track how many notes are played during star power deployment
			}
			notescore += (sustain + sustain2 + 0.5);	//Round the sustain point total (including relevant star power bonus) to the nearest integer and add to the note's score
			notescore *= score.multiplier;			//Apply the current score multiplier in effect
		}//Score the note and evaluate whammy star power gain separately

		///Update solution score
		is_solo = (tflags & EOF_NOTE_TFLAG_SOLO_NOTE) ? 1 : 0;	//Check whether this note was determined to be in a solo section
		if(is_solo)
		{	//If this note was flagged as being in a solo
			notescore += 100;	//It gets a bonus (not stackable with star power or multiplier) of 100 points if all notes in the solo are hit
		}
		score.score += notescore;

		if(logging > 1)
		{
			if(!sp_base_score)
			{	//No star power bonus
				(void) snprintf(eof_log_string, sizeof(eof_log_string) - 1, "\t\tNote #%lu (index #%lu):  \tpos = %lums, \tm pos = %.2f, \tbase = %lu, \tsustain = %lu, mult = x%lu, solo bonus = %lu, \tscore = %lu.  \tTotal score:  %lu\tSP Meter at %lu%% (uncapped %lu%%)", notectr, index, notepos, solution->note_measure_positions[index] + 1, base_score, sustain_score, score.multiplier, is_solo * 100, notescore, score.score, (unsigned long)(score.sp_meter * 100.0 + 0.5), (unsigned long)(score.sp_meter_t * 100.0 + 0.5));
			}
			else
			{	//Star power bonus
				(void) snprintf(eof_log_string, sizeof(eof_log_string) - 1, "\t\tNote #%lu (index #%lu):  \tpos = %lums, \tm pos = %.2f, \tbase = %lu, \tsustain = %lu, SP = %lu, SP sustain = %lu, mult = x%lu, solo bonus = %lu, \tscore = %lu.  \tTotal score:  %lu\tSP Meter at %lu%% (uncapped %lu%%), Deployment ends at measure %.2f", notectr, index, notepos, solution->note_measure_positions[index] + 1, base_score, sustain_score, sp_base_score, sp_sustain_score, score.multiplier, is_solo * 100, notescore, score.score, (unsigned long)(score.sp_meter * 100.0 + 0.5), (unsigned long)(score.sp_meter_t * 100.0 + 0.5), sp_deployment_end + 1);
			}
			eof_log_casual(eof_log_string, 1);
		}

		index++;	//Keep track of the number of notes in this track difficulty that were processed
	}//For each note in the track being evaluated

	solution->score = score.score;	//Set the final score to the one from the scoring state structure
	solution->deployment_notes = score.deployment_notes;
	if(logging)
	{	//Skip the overhead of building the logging string if it won't be logged
		(void) snprintf(eof_log_string, sizeof(eof_log_string) - 1, "\t\tSolution score:  %lu", solution->score);
		eof_log_casual(eof_log_string, 2);
	}

	return 0;	//Return solution evaluated
}

int eof_ch_sp_path_single_process_solve(EOF_SP_PATH_SOLUTION *best, EOF_SP_PATH_SOLUTION *testing, unsigned long first_deploy, unsigned long last_deploy, unsigned long *validcount, unsigned long *invalidcount)
{
	char windowtitle[101] = {0};
	int invalid_increment = 0;	//Set to nonzero if the last iteration of the loop manually incremented the solution due to the solution being invalid
	int retval;

	if(!eof_song || !best || !testing || (first_deploy >= best->note_count) || !validcount || !invalidcount)
		return 1;	//Invalid parameters

	testing->num_deployments = 0;	//The first solution increment will test one deployment
	while(1)
	{	//Continue testing until all solutions (or specified solutions) are tested
		unsigned long next_deploy;

		if((*validcount + *invalidcount) % 2000 == 0)
		{	//Update the title bar every 2000 solutions
			if(first_deploy == last_deploy)
			{	//If only one solution set is being tested
				(void) snprintf(windowtitle, sizeof(windowtitle) - 1, "Testing SP path solution %lu (set %lu)- Press Esc to cancel", *validcount + *invalidcount, testing->deployments[0]);
			}
			else
			{
				(void) snprintf(windowtitle, sizeof(windowtitle) - 1, "Testing SP path solution %lu (set %lu/%lu)- Press Esc to cancel", *validcount + *invalidcount, testing->deployments[0], testing->note_count);
			}
			set_window_title(windowtitle);

			if(key[KEY_ESC])
			{	//Allow user to cancel
				return 2;	//User cancellation
			}
		}

		//Increment solution
		if(!invalid_increment)
		{	//Don't increment the solution if the last iteration already did so
			if(testing->num_deployments < testing->deploy_count)
			{	//If the current solution array has fewer than the maximum number of usable star power deployments
				if(!testing->num_deployments)
				{	///Case 1:  This is the first solution
					testing->deployments[0] = first_deploy;	//Initialize the first solution to test one deployment at the specified note index
					testing->num_deployments++;	//One more deployment is in the solution
				}
				else
				{	//Add another deployment to the solution
					unsigned long previous_deploy = testing->deployments[testing->num_deployments - 1];	//This is the note at which the previous deployment occurs
					next_deploy = eof_ch_pathing_find_next_deployable_sp(testing, previous_deploy);	//Detect the next note after which another 50% of star power meter has accumulated

					if(next_deploy < testing->note_count)
					{	//If a valid placement for the next deployment was found
						///Case 2:  Add one deployment to the solution
						testing->deployments[testing->num_deployments] = next_deploy;
						testing->num_deployments++;	//One more deployment is in the solution
					}
					else
					{	//If there are no further valid notes to test for this deployment
						///Case 4:  The last deployment has exhausted all notes, remove and advance previous solution
						testing->num_deployments--;	//Remove this deployment from the solution
						if(!testing->num_deployments)
						{	//If there are no more solutions to test
							///Case 5:  All solutions exhausted
							break;
						}
						if((testing->num_deployments == 1) && (testing->deployments[testing->num_deployments - 1] + 1 > last_deploy))
						{	//If all solutions for the first deployment's specified range of notes have been tested
							///Case 6:  All specified solutions exhausted
							break;
						}
						next_deploy = testing->deployments[testing->num_deployments - 1] + 1;	//Advance the now-last deployment one note further
						if(next_deploy >= testing->note_count)
						{	//If the previous deployment cannot advance even though the deployment that was just removed had come after it
							eof_log("\tLogic error:  Can't advance previous deployment after removing last deployment (1)", 1);
							return 1;	//Return error
						}
						testing->deployments[testing->num_deployments - 1] = next_deploy;
					}
				}
			}
			else if(testing->num_deployments == testing->deploy_count)
			{	//If the maximum number of deployments are in the solution, move the last deployment to create a new solution to test
				next_deploy = testing->deployments[testing->num_deployments - 1] + 1;	//Advance the last deployment one note further
				if(next_deploy >= testing->note_count)
				{	//If the last deployment cannot advance
					///Case 4:  The last deployment has exhausted all notes, remove and advance previous solution
					testing->num_deployments--;	//Remove this deployment from the solution
					if(!testing->num_deployments)
					{	//If there are no more solutions to test
						///Case 5:  All solutions exhausted
						break;
					}
					if((testing->num_deployments == 1) && (testing->deployments[testing->num_deployments - 1] + 1 > last_deploy))
					{	//If all solutions for the first deployment's specified range of notes have been tested
						///Case 6:  All specified solutions exhausted
						break;
					}
					next_deploy = testing->deployments[testing->num_deployments - 1] + 1;	//Advance the now-last deployment one note further
					if(next_deploy >= testing->note_count)
					{	//If the previous deployment cannot advance even though the deployment that was just removed had come after it
						eof_log("\tLogic error:  Can't advance previous deployment after removing last deployment (2)", 1);
						return 1;	//Return error
					}
					testing->deployments[testing->num_deployments - 1]= next_deploy;
				}
				else
				{	///Case 3:  Advance last deployment by one note
					testing->deployments[testing->num_deployments - 1] = next_deploy;
				}
			}
			else
			{	//If num_deployments > max_deployments
				eof_log("\tLogic error:  More than the maximum number of deployments entered the testing solution", 1);
				return 1;	//Return error
			}
		}//Don't increment the solution if the last iteration already did so
		invalid_increment = 0;

		//Test and compare with the current best solution
		retval = eof_evaluate_ch_sp_path_solution(testing, *validcount + *invalidcount + 1, (eof_log_level > 1 ? 1 : 0));	//Evaluate the solution (only perform light evaluation logging if verbose logging or higher is enabled)
		if(!retval)
		{	//If the solution was considered valid
			if((testing->score > best->score) || ((testing->score == best->score) && (testing->deployment_notes < best->deployment_notes)))
			{	//If this newly tested solution achieved a higher score than the current best, or if it matched the highest score but did so with fewer notes played during star power deployment
				eof_log_casual("\t\t\t!New best solution", 2);

				best->score = testing->score;	//It is the new best solution, copy its data into the best solution structure
				best->deployment_notes = testing->deployment_notes;
				best->num_deployments = testing->num_deployments;
				best->solution_number = *validcount + *invalidcount + 1;	//Keep track of which solution number this is, for logging purposes

				memcpy(best->deployments, testing->deployments, sizeof(unsigned long) * testing->deploy_count);
			}
			(*validcount)++;	//Track the number of valid solutions tested
		}
		else
		{	//If the solution was invalid
			next_deploy = ULONG_MAX;

			if(testing->num_deployments < testing->deploy_count)
			{	//If fewer than the maximum number of deployments was just tested and found invalid, all solutions using this set of deployments will also fail
				if(testing->num_deployments > 1)
				{	//If the next deployment can be advanced by one note
					next_deploy = testing->deployments[testing->num_deployments - 1] + 1;	//Get the note index one after the last tested deployment
					if(next_deploy < testing->note_count)
					{	//If the next deployment can be advanced to that note
						if(eof_log_level > 1)
						{	//Skip the overhead of building the logging string if it won't be logged
							(void) snprintf(eof_log_string, sizeof(eof_log_string) - 1, "\t\t\t*Discarding solutions where deployment #%lu is at note index #%lu", testing->num_deployments, testing->deployments[testing->num_deployments - 1]);
							eof_log_casual(eof_log_string, 2);
						}
					}
				}
			}

			if(retval == 2)
			{	//If the last tested solution was invalidated by the cache, use the cache to skip other invalid solutions up until that cache entry's end of deployment
				if(testing->num_deployments > 1)
				{	//If the deployment that was invalidated is after the first one
					unsigned long note_end = testing->deploy_cache[testing->num_deployments - 2].note_end;	//Look up the first note occurring after the last successful star power deployment's end

					if((note_end < testing->note_count) && (note_end > testing->deployments[testing->num_deployments - 1]))
					{	//If the next deployment can be advanced to that note
						next_deploy = note_end;	//Do so
						if(eof_log_level > 1)
						{	//Skip the overhead of building the logging string if it won't be logged
							(void) snprintf(eof_log_string, sizeof(eof_log_string) - 1, "\t\t\t*Skipping deployment #%lu to next note index #%lu", testing->num_deployments, next_deploy);
							eof_log_casual(eof_log_string, 2);
						}
					}
				}
			}

			if(retval == 4)
			{	//If the last tested solution was invalidated due to there being insufficient star power to deploy, skip all other solutions up until the next star power note
				unsigned long next_sp_note;

				next_deploy = testing->deployments[testing->num_deployments - 1] + 1;	//Get the note index one after the last tested deployment
				next_sp_note = eof_ch_pathing_find_next_sp_note(testing, next_deploy);	//Find the next note with star power (first opportunity to gain more star power)

				if(next_sp_note < testing->note_count)
				{	//If such a note exists
					next_deploy = next_sp_note;	//Do so
					if(eof_log_level > 1)
					{	//Skip the overhead of building the logging string if it won't be logged
						(void) snprintf(eof_log_string, sizeof(eof_log_string) - 1, "\t\t\t*Skipping deployment #%lu to next SP note (index #%lu)", testing->num_deployments, next_deploy);
						eof_log_casual(eof_log_string, 2);
					}
				}
				else
				{	//No such star power note exists, all of the parent deployment's remaining solutions will also fail for the same reason
					//Remove the last deployment and increment the new-last deployment
					if(eof_log_level > 1)
					{	//Skip the overhead of building the logging string if it won't be logged
						(void) snprintf(eof_log_string, sizeof(eof_log_string) - 1, "\t\t\t*There are no more SP notes, the rest of the solutions for deployment #%lu are invalid.", testing->num_deployments);
						eof_log_casual(eof_log_string, 2);
					}
					testing->deployments[testing->num_deployments - 1] = testing->note_count;	//Move the current deployment to the last usable index, the normal increment at the beginning of the loop will advance the parent solution to the next index
					next_deploy = ULONG_MAX;	//Override any manual iteration from the above checks
				}
			}

			if(next_deploy < testing->note_count)
			{	//If one of the above conditional tests defined the next solution
				testing->deployments[testing->num_deployments - 1] = next_deploy;
				invalid_increment = 1;	//Prevent the solution from being incremented again at the beginning of the next loop
			}

			(*invalidcount)++;	//Track the number of invalid solutions tested
		}//If the solution was invalid
	}//Continue testing until all solutions (or specified solutions) are tested

	eof_log_casual(NULL, 1);	//Flush the buffered log writes to disk
	return 0;	//Return success
}

void eof_ch_pathing_mark_tflags(EOF_SP_PATH_SOLUTION *solution)
{
	unsigned long tracksize, numsolos, ctr, ctr2;
	EOF_PHRASE_SECTION *sectionptr;

	if(!solution || !eof_song || !solution->track || (solution->track >= eof_song->tracks))
		return;	//Invalid parameters

	tracksize = eof_get_track_size(eof_song, solution->track);
	numsolos = eof_get_num_solos(eof_song, solution->track);
	for(ctr = 0; ctr < tracksize; ctr++)
	{	//For each note in the active track
		unsigned long tflags, notepos;

		if(eof_get_note_type(eof_song, solution->track, ctr) != solution->diff)
			continue;	//If it's not in the active track difficulty, skip it

		notepos = eof_get_note_pos(eof_song, solution->track, ctr);
		tflags = eof_get_note_tflags(eof_song, solution->track, ctr);
		tflags &= ~(EOF_NOTE_TFLAG_SOLO_NOTE | EOF_NOTE_TFLAG_SP_END);	//Clear these temporary flags

		if(eof_note_is_last_in_sp_phrase(eof_song, solution->track, ctr))
		{	//If the note is the last note in a star power phrase
			tflags |= EOF_NOTE_TFLAG_SP_END;	//Set the temporary flag that will track this condition to reduce repeatedly testing for this
		}
		for(ctr2 = 0; ctr2 < numsolos; ctr2++)
		{	//For each solo phrase in the active track
			sectionptr = eof_get_solo(eof_song, solution->track, ctr2);
			if((notepos >= sectionptr->start_pos) && (notepos <= sectionptr->end_pos))
			{	//If the note is in this solo phrase
				tflags |= EOF_NOTE_TFLAG_SOLO_NOTE;	//Set the temporary flag that will track this note being in a solo
				break;
			}
		}
		eof_set_note_tflags(eof_song, solution->track, ctr, tflags);
	}
}

int eof_menu_track_find_ch_sp_path(void)
{
	EOF_SP_PATH_SOLUTION *best, *testing;
	unsigned long note_count = 0;				//The number of notes in the active track difficulty, which will be the size of the various note arrays
	double *note_measure_positions;				//The position of each note in the active track difficulty defined in measures
	double *note_beat_lengths;					//The length of each note in the active track difficulty defined in beats
	unsigned long worst_score = 0;				//The estimated maximum score when no star power is deployed
	unsigned long first_deploy = ULONG_MAX;		//The first note that occurs after the end of the second star power phrase, and is thus the first note at which star power can be deployed

	unsigned long max_deployments;				//The estimated maximum number of star power deployments, based on the amount of star power note sustain and the number of star power phrases
	unsigned long *tdeployments, *bdeployments;	//An array defining the note index number of each deployment, ie deployments[0] being the first SP deployment, deployments[1] being the second, etc.
	EOF_SP_PATH_SCORING_STATE *deploy_cache;	//An array storing cached scoring data for star power deployments

	unsigned long ctr, index, tracksize;
	unsigned long validcount = 0, invalidcount = 0;
	int error = 0;
	char undo_made = 0;
	clock_t starttime = 0, endtime = 0;
	double elapsed_time;
	unsigned long sp_phrase_count;
	double sp_sustain;				//The number of beats of star power sustain, used to count whammy bonus star power when estimating the maximum number of star power deployments

 	eof_log("eof_menu_track_find_ch_sp_path() entered", 1);

 	///Ensure there's a time signature in effect
	if(!eof_beat_stats_cached)
		eof_process_beat_statistics(eof_song, eof_selected_track);
	if(!eof_song->beat[0]->has_ts)
	{
		allegro_message("A time signature is required for the chart, but none in effect on the first beat.  4/4 will be applied.");

		eof_prepare_undo(EOF_UNDO_TYPE_NONE);
		undo_made = 1;
		eof_apply_ts(4, 4, 0, eof_song, 0);
		eof_process_beat_statistics(eof_song, eof_selected_track);	//Recalculate beat statistics
	}

	///Initialize arrays and structures
	(void) eof_count_selected_notes(&note_count);	//Count the number of notes in the active track difficulty

	if(!note_count)
		return 1;	//Don't both doing anything if there are no notes in the active track difficulty

	(void) snprintf(eof_log_string, sizeof(eof_log_string) - 1, "\tEvaluating CH path solution for \"%s\" difficulty %u", eof_song->track[eof_selected_track]->name, eof_note_type);
	eof_log(eof_log_string, 2);

	note_measure_positions = malloc(sizeof(double) * note_count);
	note_beat_lengths = malloc(sizeof(double) * note_count);
	best = malloc(sizeof(EOF_SP_PATH_SOLUTION));
	testing = malloc(sizeof(EOF_SP_PATH_SOLUTION));

	if(!note_measure_positions || !note_beat_lengths || !best || !testing)
	{	//If any of those failed to allocate
		if(note_measure_positions)
			free(note_measure_positions);
		if(note_beat_lengths)
			free(note_beat_lengths);
		if(best)
			free(best);
		if(testing)
			free(testing);

		eof_log("\tFailed to allocate memory", 1);
		return 1;
	}

	best->note_measure_positions = note_measure_positions;
	best->note_beat_lengths = note_beat_lengths;
	best->note_count = note_count;
	best->track = eof_selected_track;
	best->diff = eof_note_type;
	best->solution_number = 0;

	testing->note_measure_positions = note_measure_positions;
	testing->note_beat_lengths = note_beat_lengths;
	testing->note_count = note_count;
	testing->track = eof_selected_track;
	testing->diff = eof_note_type;
	testing->solution_number = 0;

	///Apply EOF_NOTE_TFLAG_SOLO_NOTE and EOF_NOTE_TFLAG_SP_END tflags appropriately to notes in the target track difficulty
	eof_ch_pathing_mark_tflags(best);

	///Calculate the measure position and beat length of each note in the active track difficulty
	///Find the first note at which star power can be deployed
	///Record the end position of each star power phrase for faster detection of the last note in a star power phrase
	tracksize = eof_get_track_size(eof_song, eof_selected_track);
	for(ctr = 0, index = 0; ctr < tracksize; ctr++)
	{	//For each note in the active track
		if(eof_get_note_type(eof_song, eof_selected_track, ctr) == eof_note_type)
		{	//If the note is in the active difficulty
			unsigned long notepos, notelength, ctr2, endbeat;
			double start, end, interval;

			if(index >= note_count)
			{	//Bounds check
				free(note_measure_positions);
				free(note_beat_lengths);
				free(best);
				free(testing);
				eof_log("\tNotes miscounted", 1);

				return 1;	//Logic error
			}

			//Set position and length information
			notepos = eof_get_note_pos(eof_song, eof_selected_track, ctr);
			note_measure_positions[index] = eof_get_measure_position(notepos);	//Store the measure position of the note
			notelength = eof_get_note_length(eof_song, eof_selected_track, ctr);
			start = eof_get_beat(eof_song, notepos) + (eof_get_porpos(notepos) / 100.0);	//The floating point beat position of the start of the note
			endbeat = eof_get_beat(eof_song, notepos + notelength);
			end = (double) endbeat + (eof_get_porpos(notepos + notelength) / 100.0);	//The floating point beat position of the end of the note

			//Allow for notes that end up to 2ms away from a 1/25 interval to have their beat length rounded to that interval
			interval = eof_get_beat_length(eof_song, end) / 25.0;
			for(ctr2 = 0; ctr2 < 26; ctr2++)
			{	//For each 1/25 beat interval in the note's end beat up until the next beat's position
				if(endbeat < eof_song->beats)
				{	//Error check
					double target = eof_song->beat[endbeat]->fpos + ((double) ctr2 * interval);

					if((notepos + notelength + 2 == (unsigned long) (target + 0.5)) || (notepos + notelength + 1 == (unsigned long) (target + 0.5)))
					{	//If the note ends 1 or 2 ms before this 1/25 beat interval
						end = eof_get_beat(eof_song, notepos + notelength) + ((double) ctr2 / 25.0);	//Re-target the note's end position exactly to this interval
					}
					else if((notepos + notelength - 2 == (unsigned long) (target + 0.5)) || (notepos + notelength - 1 == (unsigned long) (target + 0.5)))
					{	//If the note ends 1 or 2 ms after this 1/25 beat interval
						end = eof_get_beat(eof_song, notepos + notelength) + ((double) ctr2 / 25.0);	//Re-target the note's end position exactly to this interval
					}
				}
			}

			note_beat_lengths[index] = end - start;		//Store the floating point beat length of the note

			index++;
		}//If the note is in the active difficulty
	}//For each note in the active track

	///Estimate the maximum number of star power deployments, to limit the number of solutions tested
	sp_phrase_count = 0;
	sp_sustain = 0.0;
	for(index = 0; index < note_count; index++)
	{	//For each note in the active track difficulty
		unsigned long note = eof_translate_track_diff_note_index(eof_song, eof_selected_track, eof_note_type, index);

		if(note < tracksize)
		{	//If the real note number was identified
			unsigned long noteflags = eof_get_note_flags(eof_song, eof_selected_track, note);
			if(noteflags & EOF_NOTE_FLAG_SP)
			{	//If the note has star power
				if(eof_get_note_length(eof_song, eof_selected_track, note) > 1)
				{	//If the note has sustain
					sp_sustain += note_beat_lengths[index];	//Count the number of beats of star power that will be whammied for bonus star power
				}
				if(eof_get_note_tflags(eof_song, eof_selected_track, note) & EOF_NOTE_TFLAG_SP_END)
				{	//If this is the last note in a star power phrase
					sp_phrase_count++;	//Keep count to determine how much star power is gained throughout the track difficulty
				}
			}
		}
	}
	sp_phrase_count += (sp_sustain / 8.0) + 0.0001;	//Each beat of whammied star power sustain awards 1/32 meter of star power (1/8 as much star power as a completed star power phrase), (allow variance for math error)
	max_deployments = sp_phrase_count / 2;			//Two star power phrases' worth of star power is enough to deploy star power once

	///Initialize deployment arrays
	tdeployments = malloc(sizeof(unsigned long) * max_deployments);
	bdeployments = malloc(sizeof(unsigned long) * max_deployments);
	deploy_cache = malloc(sizeof(EOF_SP_PATH_SCORING_STATE) * max_deployments);

	if(!tdeployments || !bdeployments || !deploy_cache)
	{
		free(note_measure_positions);
		free(note_beat_lengths);
		free(best);
		free(testing);
		if(tdeployments)
			free(tdeployments);
		if(bdeployments)
			free(bdeployments);
		if(deploy_cache)
			free(deploy_cache);

		eof_log("\tFailed to allocate memory", 1);
		return 1;
	}

	for(ctr = 0; ctr < max_deployments; ctr++)
	{	//For every entry in the deploy cache
		deploy_cache[ctr].note_start = ULONG_MAX;	//Mark it as invalid
	}
	best->deployments = bdeployments;
	best->deploy_cache = deploy_cache;
	best->deploy_count = max_deployments;
	testing->deployments = tdeployments;
	testing->deploy_cache = deploy_cache;
	testing->deploy_count = max_deployments;

	///Determine the maximum score when no star power is deployed
	best->num_deployments = 0;
	eof_log_casual("CH Scoring without star power:", 1);
	if(eof_evaluate_ch_sp_path_solution(best, 0, 2))
	{	//Populate the "best" solution with the worst scoring solution, so any better solution will replace it, verbose log the evaluation
		eof_log("\tError scoring no star power usage", 1);
		allegro_message("Error scoring no star power usage");
		error = 1;	//The testing of all other solutions will be skipped
	}
	else
	{
		worst_score = best->score;
		(void) snprintf(eof_log_string, sizeof(eof_log_string) - 1, "\tEstimated maximum score without deploying star power is %lu.", worst_score);
		eof_log_casual(eof_log_string, 1);
	}

	first_deploy = eof_ch_pathing_find_next_deployable_sp(testing, 0);	//Starting from the first note in the target track difficulty, find the first note at which star power can be deployed
	if(first_deploy < note_count)
	{	//If the note was identified
		(void) snprintf(eof_log_string, sizeof(eof_log_string) - 1, "\tFirst possible star power deployment is at note index %lu in this track difficulty.", first_deploy);
		eof_log_casual(eof_log_string, 1);
	}
	else
	{
		eof_log("\tError detecting first available star power deployment", 1);
		error = 1;
	}

	(void) snprintf(eof_log_string, sizeof(eof_log_string) - 1, "\tEstimated maximum number of star power deployments is %lu", max_deployments);
	eof_log_casual(eof_log_string, 1);
	eof_log_casual(NULL, 1);	//Flush the buffered log writes to disk

	///Test all possible solutions to find the highest scoring one
	if(!error)
	{	//If the no deployments score and first available star power deployment were successfully determined
		starttime = clock();	//Track the start time
		error = eof_ch_sp_path_single_process_solve(best, testing, first_deploy, ULONG_MAX, &validcount, &invalidcount);	//Test all solutions
		endtime = clock();	//Track the end time
		elapsed_time = (double)(endtime - starttime) / (double)CLOCKS_PER_SEC;	//Convert to seconds
	}

	///Report best solution
	eof_log_casual(NULL, 1);	//Flush the buffered log writes to disk
	eof_fix_window_title();
	if(error == 1)
	{
		if(!max_deployments)
		{
			allegro_message("There are not enough star power phrases/sustains to deploy even once.");
		}
		else
		{
			allegro_message("Failed to detect optimum star power path.");
		}
	}
	else if(error == 2)
	{
		set_window_title("Release Escape key...");
		while(key[KEY_ESC])
		{	//Wait for user to release Escape key
			Idle(10);
		}
		eof_fix_window_title();
		(void) snprintf(eof_log_string, sizeof(eof_log_string) - 1, "\t%lu solutions tested (%lu valid, %lu invalid) in %.2f seconds (%.2f solutions per second)", validcount + invalidcount, validcount, invalidcount, elapsed_time, ((double)validcount + invalidcount)/elapsed_time);
		eof_log(eof_log_string, 1);
		allegro_message("User cancellation.%s", eof_log_string);
	}
	else
	{
		//If logging is enabled, verbose log the best solution
		if(eof_log_level)
		{
			//Prep the testing array to hold the best solution for evaluation and logging
			memcpy(testing->deployments, best->deployments, sizeof(unsigned long) * best->num_deployments);
			testing->num_deployments = best->num_deployments;
			for(ctr = 0; ctr < testing->deploy_count; ctr++)
			{	//For each entry in the deploy cache
				testing->deploy_cache[ctr].note_start = ULONG_MAX;	//Invalidate the entry
			}
			eof_log_casual("Best solution:", 1);
			(void) eof_evaluate_ch_sp_path_solution(testing, best->solution_number, 2);
		}
		(void) snprintf(eof_log_string, sizeof(eof_log_string) - 1, "\t%lu solutions tested (%lu valid, %lu invalid) in %.2f seconds (%.2f solutions per second)", validcount + invalidcount, validcount, invalidcount, elapsed_time, ((double)validcount + invalidcount)/elapsed_time);
		eof_log_casual(NULL, 1);	//Flush the buffered log writes to disk
		eof_log(eof_log_string, 1);
		if((best->deployment_notes == 0) && (best->score > worst_score))
		{	//If the best score did not reflect notes being played in star power, but the score somehow was better than the score from when no star power deployment was tested
			eof_log("\tLogic error calculating solution.", 1);
			allegro_message("Logic error calculating solution.");
		}
		else if(best->deployment_notes == 0)
		{	//If there was no best use of star power
			eof_log("\tNo notes were playable during a star power deployment", 1);
			allegro_message("No notes were playable during a star power deployment");
		}
		else
		{	//There is at least one note played during star power deployment
			unsigned long notenum, min, sec, ms, flags;
			char timestamps[500], indexes[500], tempstring[20], scorestring[50];
			char *resultstring1 = "Optimum star power deployment in Clone Hero for this track difficulty is at these note timestamps (highlighted):";

			(void) snprintf(eof_log_string, sizeof(eof_log_string) - 1, "\t%s", resultstring1);
			eof_log(eof_log_string, 1);
			timestamps[0] = '\0';	//Empty the timestamps string
			indexes[0] = '\0';		//And the indexes string

			for(ctr = 0; ctr < best->num_deployments; ctr++)
			{	//For each deployment in the best solution
				notenum = eof_translate_track_diff_note_index(eof_song, best->track, best->diff, best->deployments[ctr]);	//Find the real note number for this index
				if(notenum >= tracksize)
				{	//If the note was not identified
					eof_log("\tLogic error displaying solution.", 1);
					allegro_message("Logic error displaying solution.");
					break;
				}

				//Build string of deployment timestamps
				ms = eof_get_note_pos(eof_song, eof_selected_track, notenum);	//Determine mm:ss.ms formatting of timestamp
				min = ms / 60000;
				ms -= 60000 * min;
				sec = ms / 1000;
				ms -= 1000 * sec;
				snprintf(tempstring, sizeof(tempstring) - 1, "%s%lu:%lu.%lu", (ctr ? ", " : ""), min, sec, ms);	//Generate timestamp, prefixing with a comma and spacing after the first
				strncat(timestamps, tempstring, sizeof(timestamps) - 1);	//Append to timestamps string

				flags = eof_get_note_flags(eof_song, eof_selected_track, notenum);
				if(!(flags & EOF_NOTE_FLAG_HIGHLIGHT) && !undo_made)
				{	//If this is the first note being highlighted for the solution
					eof_prepare_undo(EOF_UNDO_TYPE_NONE);
					undo_made = 1;
				}
				flags |= EOF_NOTE_FLAG_HIGHLIGHT;	//Enable highlighting for this note
				eof_set_note_flags(eof_song, eof_selected_track, notenum, flags);

				snprintf(tempstring, sizeof(tempstring) - 1, "%s%lu", (ctr ? ", " : ""), best->deployments[ctr]);
				strncat(indexes, tempstring, sizeof(indexes) - 1);	//Append to indexes string
			}
			(void) snprintf(eof_log_string, sizeof(eof_log_string) - 1, "\t%s", timestamps);
			eof_log(eof_log_string, 1);
			eof_log("\tAt these note indexes:", 1);
			(void) snprintf(eof_log_string, sizeof(eof_log_string) - 1, "\t%s", indexes);
			eof_log(eof_log_string, 1);
			(void) snprintf(scorestring, sizeof(scorestring) - 1, "Estimated score:  %lu", best->score);
			(void) snprintf(eof_log_string, sizeof(eof_log_string) - 1, "\t%s", scorestring);
			eof_log(eof_log_string, 1);
			(void) eof_detect_difficulties(eof_song, eof_selected_track);	//Update highlighting variables
			eof_render();
			allegro_message("%s\n%s\n%s", resultstring1, timestamps, scorestring);
		}
	}

	///Cleanup
	free(note_measure_positions);
	free(note_beat_lengths);
	free(tdeployments);
	free(bdeployments);
	free(deploy_cache);
	free(best);
	free(testing);
	eof_log_casual(NULL, 1);	//Flush the buffered log writes to disk, if any are left

	for(ctr = 0; ctr < tracksize; ctr++)
	{	//For each note in the active track
		unsigned long tflags = eof_get_note_tflags(eof_song, eof_selected_track, ctr);

		tflags &= ~EOF_NOTE_TFLAG_SOLO_NOTE;	//Clear the temporary flags
		tflags &= ~EOF_NOTE_TFLAG_SP_END;
		eof_set_note_tflags(eof_song, eof_selected_track, ctr, tflags);
	}

	return 1;
}

void eof_ch_sp_path_worker(char *job_file)
{
	PACKFILE *inf, *outf;
	char output_path[1024] = {0};
	char project_path[1024];
	int error = 0, canceled = 0, retval;
	EOF_SP_PATH_SOLUTION best = {0}, testing = {0};
	unsigned long ctr, first_deploy, last_deploy, validcount = 0, invalidcount = 0;
	clock_t end_time;

	//Parse job file
	if(!job_file)
	{	//Invalid parameter
		error = 1;
	}
	else
	{	//A job file path was specified
		inf = pack_fopen(job_file, "rb");
		if(!inf)
		{	//If the job file can't be loaded
			error = 1;
		}
		else if(eof_load_song_string_pf(project_path, inf, sizeof(project_path)))
		{	//If the project file path can't be read
			error = 1;
		}
		else
		{	//The project file path was read
			eof_song = eof_load_song(project_path);
			if(!eof_song)
			{	//If the project file couldn't be loaded
				error = 1;
			}
			else
			{	//The project file was loaded
				//Finish setup after loading project
				eof_song_loaded = 1;
				eof_init_after_load(0);
				eof_fixup_notes(eof_song);

				//Initialize solution array with job file contents
				best.deploy_count = pack_igetl(inf);
				testing.deploy_count = best.deploy_count;
				best.deployments = malloc(sizeof(unsigned long) * best.deploy_count);
				testing.deployments = malloc(sizeof(unsigned long) * best.deploy_count);
				best.deploy_cache = malloc(sizeof(EOF_SP_PATH_SCORING_STATE) * best.deploy_count);
				testing.deploy_cache = best.deploy_cache;
				if(best.deploy_cache)
				{	//If the deploy cache array was allocated
					for(ctr = 0; ctr < best.deploy_count; ctr++)
					{	//For every entry in the deploy cache
						best.deploy_cache[ctr].note_start = ULONG_MAX;	//Mark it as invalid
					}
				}

				best.note_count = pack_igetl(inf);
				testing.note_count = best.note_count;
				best.note_measure_positions = malloc(sizeof(double) * best.note_count);
				testing.note_measure_positions = best.note_measure_positions;
				if(best.note_measure_positions)
				{	//If the measure positions array was allocated
					for(ctr = 0; ctr < best.note_count; ctr++)
					{	//For every entry in the measure position array
						double temp = 0.0;

						if(pack_fread(&temp, sizeof(double), inf) != sizeof(double))
						{	//If the double floating point value was not read
							error = 1;
							break;
						}
						else
						{
							best.note_measure_positions[ctr] = temp;
						}
					}
				}

				best.note_beat_lengths = malloc(sizeof(double) * best.note_count);
				testing.note_beat_lengths = best.note_beat_lengths;
				if(best.note_beat_lengths)
				{	//If the note lengths array was allocated
					for(ctr = 0; ctr < best.note_count; ctr++)
					{	//For every entry in the note lengths array
						double temp = 0.0;

						if(pack_fread(&temp, sizeof(double), inf) != sizeof(double))
						{	//If the double floating point value was not read
							error = 1;
							break;
						}
						else
						{
							best.note_beat_lengths[ctr] = temp;
						}
					}
				}

				best.track = pack_igetl(inf);
				testing.track = best.track;
				best.diff = pack_igetw(inf);
				testing.diff = best.diff;
				first_deploy = pack_igetl(inf);
				last_deploy = pack_igetl(inf);
				best.num_deployments = testing.num_deployments = 0;
				best.score = testing.score = 0;
			}//The project file was loaded
		}//The project file path was read
		pack_fclose(inf);
	}//A job file path was specified
	if(!best.deployments || !testing.deployments || !best.deploy_cache || !best.note_measure_positions || !best.note_beat_lengths)
	{	//If any of the arrays failed to allocate
		error = 1;
	}

	//Test solutions
	if(!error)
	{	//If an error hasn't occurred yet
		char buffer[300];

		//Set the target track difficulty and apply temporary flags as appropriate
		eof_selected_track = best.track;
		eof_note_type = best.diff;
		(void) eof_detect_difficulties(eof_song, eof_selected_track);
		eof_ch_pathing_mark_tflags(&testing);

		clear_to_color(eof_window_info->screen, eof_color_gray);
		(void) snprintf(buffer, sizeof(buffer) - 1, "Testing solutions where first deployment is between note index %lu and %lu", first_deploy, last_deploy);
		textout_ex(eof_window_info->screen, eof_font, buffer, 0, 0, eof_color_white, -1);	//Print the worker parameters to the program window

		#ifdef EOF_DEBUG
			(void) snprintf(buffer, sizeof(buffer) - 1, "Job file:  %s", get_filename(job_file));
			textout_ex(eof_window_info->screen, eof_font, buffer, 0, 12, eof_color_white, -1);	//Print the worker parameters to the program window
			(void) snprintf(buffer, sizeof(buffer) - 1, "deploy_count = %lu, note_count = %lu, track = %lu, diff = %d", testing.deploy_count, testing.note_count, testing.track, testing.diff);
			textout_ex(eof_window_info->screen, eof_font, buffer, 0, 24, eof_color_white, -1);	//Print the worker parameters to the program window
			(void) snprintf(buffer, sizeof(buffer) - 1, "measure positions : Note 0 = %f, Note %lu = %f", testing.note_measure_positions[0], testing.note_count - 1, testing.note_measure_positions[testing.note_count - 1]);
			textout_ex(eof_window_info->screen, eof_font, buffer, 0, 36, eof_color_white, -1);	//Print the worker parameters to the program window
			(void) snprintf(buffer, sizeof(buffer) - 1, "note beat lengths : Note 0 = %f, Note %lu = %f", testing.note_beat_lengths[0], testing.note_count - 1, testing.note_beat_lengths[testing.note_count - 1]);
			textout_ex(eof_window_info->screen, eof_font, buffer, 0, 48, eof_color_white, -1);	//Print the worker parameters to the program window
		#endif

		//Test the specified range of solutions
		(void) replace_extension(output_path, job_file, "running", sizeof(output_path));	//Build the path to the file that will signal to the supervisor that the worker is beginning processing
		outf = pack_fopen(output_path, "w");	//Create that file
		(void) pack_fclose(outf);
		retval = eof_ch_sp_path_single_process_solve(&best, &testing, first_deploy, last_deploy, &validcount, &invalidcount);
		if(retval == 1)
		{	//If the solution testing failed
			error = 1;
		}
		else if(retval == 2)
		{	//If the testing was canceled
			canceled = 1;
		}
	}

	if(error)
	{	//If the job was not successfully processed
		(void) replace_extension(output_path, job_file, "fail", 1024);		//Create a results file to specify failure
		outf = pack_fopen(output_path, "w");

	}
	else if(canceled)
	{	//If the user canceled the job
		(void) replace_extension(output_path, job_file, "cancel", 1024);	//Create a results file to specify cancelation
		outf = pack_fopen(output_path, "w");
	}
	else
	{	//Normal completion, write best solution information to disk
		(void) replace_extension(output_path, job_file, "success", 1024);	//Create a results file to contain the best solution
		outf = pack_fopen(output_path, "w");
		pack_iputl(best.score, outf);
		pack_iputl(best.deployment_notes, outf);
		pack_iputl(validcount, outf);
		pack_iputl(invalidcount, outf);
		pack_iputl(best.num_deployments, outf);
		for(ctr = 0; ctr < best.num_deployments; ctr++)
		{	//For each deployment in the solution
			pack_iputl(best.deployments[ctr], outf);	//Write it to disk
		}
		end_time = clock();
		pack_fwrite(&end_time, sizeof(end_time), outf);	//Write the clock time to disk
	}

	//Cleanup
	if(best.deployments)
		free(best.deployments);
	if(testing.deployments)
		free(testing.deployments);
	if(best.deploy_cache)
		free(best.deploy_cache);
	if(best.note_measure_positions)
		free(best.note_measure_positions);
	if(best.note_beat_lengths)
		free(best.note_beat_lengths);

	pack_fclose(outf);	//Close the results file
	(void) replace_extension(output_path, job_file, "running", sizeof(output_path));
	(void) delete_file(output_path);	//Delete the running status file to indicate the worker is finished
	(void) delete_file(job_file);		//Then delete the job file to signal to the supervisor process that the results file is ready to access
}

int eof_ch_sp_path_supervisor_process_solve(EOF_SP_PATH_SOLUTION *best, EOF_SP_PATH_SOLUTION *testing, unsigned long first_deploy, unsigned long worker_count, unsigned long *validcount, unsigned long *invalidcount)
{
	EOF_SP_PATH_WORKER *workers;
	PACKFILE *fp;
	unsigned long ctr, workerctr, solutionctr, first_worker_solution_count = 0, last_worker_solution_count = 0, num_workers_running;
	char filename[50];	//Stores the job filename for worker processes
	char commandline[1050];	//Stores the command line for launching a worker process
	char tempstr[100];
	int done = 0, error = 0, canceled = 0, workerstatuschange;

	if(!best || !testing || !validcount || !invalidcount || !worker_count)
		return 1;	//Invalid parameters

	//Initialize worker array
	workers = malloc(sizeof(EOF_SP_PATH_WORKER) * worker_count);
	if(!workers)
		return 1;	//Couldn't allocate memory
	for(ctr = 0; ctr < worker_count; ctr++)
	{	//For each worker entry
		workers[ctr].status = EOF_SP_PATH_WORKER_IDLE;
	}

	//Initialize temp folder and job file path array
	if(eof_validate_temp_folder())
	{	//Ensure the correct working directory and presence of the temporary folder
		eof_log("\tCould not validate working directory and temp folder", 1);
		return 1;	//Return failure
	}

	solutionctr = first_deploy;	//The first worker process will be directed to calculate this solution set
	while(!done && !error && !canceled)
	{	//Until the supervisor's job is completed
		workerstatuschange = num_workers_running = 0;

		//Check worker process status
		for(workerctr = 0; workerctr < worker_count; workerctr++)
		{	//For each worker process
			(void) snprintf(filename, sizeof(filename) - 1, "%s%lu.job", eof_temp_path_s, workerctr);	//Build the file path for the process's job/results file

			if(workers[workerctr].status == EOF_SP_PATH_WORKER_IDLE)
			{	//If this process can be dispatched
				if(!error && !canceled && (solutionctr <= testing->note_count))
				{	//If the supervisor hasn't detected a failed/canceled status, and if there are solutions left to test
					fp = pack_fopen(filename, "wb");	//Create the job file
					if(!fp)
					{	//If the file couldn't be created
						error = 1;
					}
					else
					{	//The job file was opened for writing
						unsigned long last_deploy;

						//Build the job file
						if(first_worker_solution_count && last_worker_solution_count)
						{	//If at least two workers have already completed
							last_deploy = solutionctr + (first_worker_solution_count / last_worker_solution_count);	//Ramp up the number of assigned solution sets as each set gets smaller
						}
						if(last_deploy > testing->note_count)
						{	//If this worker is processing the last of the solution sets
							last_deploy = testing->note_count;	//Bounds check this variable so that the correct value is given to the worker
						}
						(void) eof_save_song_string_pf(eof_filename, fp);	//Write the path to the EOF project file
						(void) pack_iputl(testing->deploy_count, fp);	//Write the detected maximum number of deployments so the worker can create an appropriately sized depoyment and cache arrays
						(void) pack_iputl(testing->note_count, fp);		//Write the number of notes in the target track difficulty
						for(ctr = 0; ctr < testing->note_count; ctr++)
						{	//For every entry in the note measure position array
							if(pack_fwrite(&testing->note_measure_positions[ctr], sizeof(double), fp) != sizeof(double))	//Write the note measure position
							{	//If the double floating point value was not written
								error = 1;
								break;
							}
						}
						for(ctr = 0; ctr < testing->note_count; ctr++)
						{	//For every entry in the note beat lengths array
							if(pack_fwrite(&testing->note_beat_lengths[ctr], sizeof(double), fp) != sizeof(double))	//Write the note beat length
							{	//If the double floating point value was not written
								error = 1;
								break;
							}
						}
						(void) pack_iputl(testing->track, fp);	//Write the target track
						(void) pack_iputw(testing->diff, fp);	//Write the target difficulty
						(void) pack_iputl(solutionctr, fp);		//Write the beginning of the solution set range the worker is to test
						(void) pack_iputl(last_deploy, fp);		//Write the ending of the solution set range the worker is to test
						(void) pack_fclose(fp);

						//Launch the worker process
						if(!error)
						{	//If there hasn't been an I/O error
							get_executable_name(commandline, sizeof(commandline) - 1);	//Get the full path to the running EOF executable
							snprintf(tempstr, sizeof(tempstr) - 1, " -ch_sp_path_worker \"%s\"", filename);	//Build the parameters to invoke EOF as a worker process to handle this job file
							strncat(commandline, tempstr, sizeof(commandline) - 1);		//Build the full command line to launch the EOF worker process

							(void) snprintf(eof_log_string, sizeof(eof_log_string) - 1, "\tLaunching worker process #%lu to evaluate solution sets #%lu-#%lu with command line:  %s", workerctr, solutionctr, last_deploy, commandline);
							eof_log_casual(eof_log_string, 1);

							if(eof_system(commandline))
							{	//If the command failed to run
								eof_log_casual("\t\tFailed to launch process", 1);
								error = 1;
							}
							else
							{
								(void) replace_extension(filename, filename, "running", sizeof(filename));	//Build the path to this worker's running status file
								for(ctr = 0; ctr < 3; ctr++)
								{	//Attempt to detect the worker process up to three times
									if(exists(filename))	//If the worker process created this file to signal that it is processing the job
										break;
									Idle(10);				//Wait 10ms to check again
								}
								if(ctr == 3)
								{	//If the worker process was not detected
									eof_log_casual("\t\tFailed to detect worker process", 1);
									error = 1;
								}
								else
								{	//The worker process is running
									workers[workerctr].status = EOF_SP_PATH_WORKER_RUNNING;
									workerstatuschange = 1;
									solutionctr += last_deploy;	//Update the counter tracking which solution set to assign next
								}
							}
						}
					}//The job file was opened for writing
				}//If the supervisor hasn't detected a failed/canceled status, and if there are solutions left to test
			}//If this process can be dispatched
			else if(workers[workerctr].status == EOF_SP_PATH_WORKER_RUNNING)
			{	//If this process was previously dispatched, check if it's done
				num_workers_running++;	//Track the number of workers in running status
				(void) replace_extension(filename, filename, "job", sizeof(filename));	//Build the path to this worker's job file
				if(!exists(filename))
				{	//If the job file no longer exists, the worker has completed, check the results
					(void) replace_extension(filename, filename, "fail", sizeof(filename));	//Build the path to check whether this worker process failed
					if(exists(filename))
					{	//If the worker failed
						error = 1;
					}
					else
					{
						(void) replace_extension(filename, filename, "cancel", sizeof(filename));	//Build the path to check whether this worker process was canceled
						if(exists(filename))
						{	//If the user canceled the worker
							canceled = 1;
						}
						else
						{
							(void) replace_extension(filename, filename, "success", sizeof(filename));	//Build the path to check whether this worker process succeeded
							if(exists(filename))
							{	//If the worker succeeded
								fp = pack_fopen(filename, "rb");
								if(!fp)
								{	//If the results couldn't be accessed
									(void) snprintf(eof_log_string, sizeof(eof_log_string) - 1, "\tFailed to open \"%s\" results file", filename);
									eof_log_casual(eof_log_string, 1);
									error = 1;
								}
								else
								{
									unsigned long vcount, icount;
									char tempstring[30];
									double elapsed_time;

									//Parse the results
									testing->score = pack_igetl(fp);
									testing->deployment_notes = pack_igetl(fp);
									vcount = pack_igetl(fp);
									*validcount += vcount;
									icount = pack_igetl(fp);
									*invalidcount += icount;
									if(!first_worker_solution_count)
									{	//If this is the first completed worker process
										first_worker_solution_count = vcount + icount;	//Track the number of solutions it processed
									}
									else
									{	//Otherwise use this to determine whether to increase the number of solutions assigned to the next worker
										last_worker_solution_count = vcount + icount;
									}
									testing->num_deployments = pack_igetl(fp);	//The number of deployments in the solution
									for(ctr = 0; ctr < testing->num_deployments; ctr++)
									{	//For each deployment in the solution
										testing->deployments[ctr] = pack_igetl(fp);	//Read the deployment's note index
									}
									(void) pack_fread(&workers[workerctr].end_time, sizeof(clock_t), fp);	//Read the end time of the worker
									(void) pack_fclose(fp);

									//Log the results
									elapsed_time = (double)(workers[workerctr].end_time - workers[workerctr].start_time) / (double)CLOCKS_PER_SEC;	//Convert to seconds
									(void) snprintf(eof_log_string, sizeof(eof_log_string) - 1, "\tWorker process %lu completed evaluation of solution sets #%lu-#%lu in %.2f seconds.  Its best solution:  Deploy at indexes ", workerctr, workers[workerctr].first_deploy, workers[workerctr].last_deploy, elapsed_time);
									eof_log_casual(eof_log_string, 1);
									for(ctr = 0; ctr < testing->num_deployments; ctr++)
									{	//For each deployment in the chosen solution
										snprintf(tempstring, sizeof(tempstring) - 1, "%s%lu", (ctr ? ", " : ""), testing->deployments[ctr]);
										strncat(eof_log_string, tempstring, sizeof(eof_log_string) - 1);
									}
									snprintf(tempstring, sizeof(tempstring) - 1, ".  Score = %lu", testing->score);
									strncat(eof_log_string, tempstring, sizeof(eof_log_string) - 1);

									//Compare with current best solution
									if((testing->score > best->score) || ((testing->score == best->score) && (testing->deployment_notes < best->deployment_notes)))
									{	//If this worker's solution is the best solution
										eof_log_casual("\t\t!New best solution", 1);

										best->score = testing->score;	//It is the new best solution, copy its data into the best solution structure
										best->deployment_notes = testing->deployment_notes;
										best->num_deployments = testing->num_deployments;

										memcpy(best->deployments, testing->deployments, sizeof(unsigned long) * testing->deploy_count);
									}

									//Update the worker status
									workers[workerctr].status = EOF_SP_PATH_WORKER_IDLE;
									workerstatuschange = 1;
								}
							}//If the worker succeeded
							else
							{	//None of the expected results file names were found
								eof_log_casual("\tWorker failed to report status.", 1);
								error = 1;
							}
							if(!error)
							{	//If the expected results file was found, delete it
								(void) delete_file(filename);
							}
						}
					}
				}//If the job file no longer exists, the worker has completed, check the results
				if(error)
				{
					set_window_title("SP pathing failed.  Waiting for workers to finish/cancel.");
					workers[workerctr].status = EOF_SP_PATH_WORKER_FAILED;
					workerstatuschange = 1;
				}
			}//If this process was previously dispatched, check if it's done
		}//For each worker process

		//Check for user cancellation in the supervisor process
		if(key[KEY_ESC])
		{
			canceled = 1;
		}

		if(!num_workers_running && (solutionctr > testing->note_count))
		{	//If no workers are in running state and all solution sets have already been assigned to workers
			done = 1;
		}
		if(!workerstatuschange)
		{	//If all workers are in the same status as during the previous check
			Idle(10);	//Wait a bit before checking worker process statuses again
		}
		else
		{
			int ypos = 0;
			eof_log_casual(NULL, 1);	//Flush the buffered log writes to disk

			///Blit each worker status to the screen
			clear_to_color(eof_window_note_upper_left->screen, eof_color_gray);
			for(workerctr = 0; workerctr < worker_count; workerctr++)
			{	//For each worker process
				char *idle_status = "IDLE";
				char *running_status = "RUNNING";
				char *error_status = "ERROR";
				char *current_status;

				if(workers[workerctr].status == EOF_SP_PATH_WORKER_IDLE)
					current_status = idle_status;
				else if(workers[workerctr].status == EOF_SP_PATH_WORKER_RUNNING)
					current_status = running_status;
				else
					current_status = error_status;

				if(workers[workerctr].first_deploy == workers[workerctr].last_deploy)
				{	//Worker process is only evaluating one solution set
					snprintf(tempstr, sizeof(tempstr) - 1, "Worker process #%lu status: %s\tEvaluating solution #%lu", workerctr, current_status, workers[workerctr].first_deploy);
				}
				else
				{	//Worker process is evaluating multiple solution sets
					snprintf(tempstr, sizeof(tempstr) - 1, "Worker process #%lu status: %s\tEvaluating solutions #%lu through #%lu", workerctr, current_status, workers[workerctr].first_deploy, workers[workerctr].last_deploy);
				}
				textout_ex(eof_window_note_upper_left->screen, eof_font, tempstr, 2, ypos, eof_color_white, -1);
				ypos += 12;
			}
			if(error)
			{
				textout_ex(eof_window_note_upper_left->screen, eof_font, "Error encountered.  Waiting for workers to finish/cancel.", 2, ypos, eof_color_white, -1);
			}
			else if(canceled)
			{
				textout_ex(eof_window_note_upper_left->screen, eof_font, "User cancelation.  Waiting for workers to finish/cancel.", 2, ypos, eof_color_white, -1);
			}
			else
			{
				textout_ex(eof_window_note_upper_left->screen, eof_font, "Press Escape to cancel.", 2, ypos, eof_color_white, -1);
			}
			snprintf(tempstr, sizeof(tempstr) - 1, "%lu workers running.  Last assigned solution set is %lu/%lu.", num_workers_running, (solutionctr != first_deploy) ? (solutionctr - 1) : first_deploy, testing->note_count);
			set_window_title(tempstr);
		}
	}//Until the supervisor's job is completed

	//Clean up
	free(workers);

	eof_log_casual(NULL, 1);	//Flush the buffered log writes to disk, if any
	return 0;	//Return success
}
