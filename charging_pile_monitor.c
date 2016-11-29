#include "postgres.h"
#include "executor/spi.h"       /* this is what you need to work with SPI */
#include "commands/trigger.h"   /* ... triggers ... */
#include "utils/rel.h"          /* ... and relations */

#ifdef PG_MODULE_MAGIC
PG_MODULE_MAGIC;
#endif

extern Datum charging_pile_monitor(PG_FUNCTION_ARGS);

PG_FUNCTION_INFO_V1(charging_pile_monitor);

Datum
charging_pile_monitor(PG_FUNCTION_ARGS)
{
	TriggerData *trigdata = (TriggerData *) fcinfo->context;
	TupleDesc   tupdesc;
	HeapTuple   rettuple;

	char        opcode = 'X'; // U for update I for insert D for delete X for unknow

	/* make sure it's called as a trigger at all */
	if (!CALLED_AS_TRIGGER(fcinfo))
		elog(ERROR, "charging_pile_monitor: not called by trigger manager");

	/* tuple to return to executor */
	if (TRIGGER_FIRED_BY_UPDATE(trigdata->tg_event)){
		opcode = 'U';
		rettuple = trigdata->tg_newtuple;
	}else{
		if (TRIGGER_FIRED_BY_INSERT(trigdata->tg_event)) {
			opcode = 'I';
		}else if(TRIGGER_FIRED_BY_DELETE(trigdata->tg_event)){
			opcode = 'D';
		}
		rettuple = trigdata->tg_trigtuple;
	}

	tupdesc = trigdata->tg_relation->rd_att;

	char notify[1024] = {0};
	unsigned int notifylen = 0;
	unsigned int natts = trigdata->tg_relation->rd_att->natts;
	char* value ;
	unsigned int valuelen = 0;
	notify[0] = opcode;
	notify[1] = '^';
	notifylen = 2;

	elog(INFO, "nattr %d",  natts);
	
	int i = 0;
	for(; i < natts; ++i){
		if (tupdesc->attrs[i]->attisdropped)
			continue;

		value = SPI_getvalue(rettuple, tupdesc, i+1);
		if (value == NULL) {
			value = "$";
		}
		valuelen = strlen(value);
		memcpy(notify+notifylen, value,valuelen); 
		*(notify+notifylen+valuelen) = '^';
		notifylen = notifylen+valuelen+1;
	}
	notify[--notifylen] = 0;
	elog(INFO, "%s", notify);

	int ret;
	char *tablename = " t_charge_pile,";
	int tablenamelen = strlen(tablename);
	if ((ret = SPI_connect()) < 0)
		elog(ERROR, "SPI connect return %d", ret);

	/* get number of rows in table */
	char notifycmd[1024] = {0};
	unsigned int notifycmdlen = sizeof("NOTIFY")-1;
	memcpy(notifycmd, "NOTIFY", notifycmdlen);
	notifycmd[notifycmdlen++] = ' ';
	memcpy(&notifycmd[notifycmdlen], tablename, tablenamelen);
	notifycmdlen += tablenamelen;
	notifycmd[notifycmdlen++] = '\'';
	memcpy(&notifycmd[notifycmdlen], notify, notifylen);
	notifycmdlen += notifylen;
	notifycmd[notifycmdlen++] = '\'';
	elog(INFO, "%s", notifycmd);

	ret = SPI_exec(notifycmd,0);

	if (ret < 0)
		elog(ERROR, " SPI_exec returned %d", ret);

	SPI_finish();

	return PointerGetDatum(rettuple);
}
