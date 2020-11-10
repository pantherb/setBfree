#define _XOPEN_SOURCE 700

#include "global_inst.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void
midnam_header (FILE* f, char* model)
{
	fprintf (f,
	         "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
	         "<!DOCTYPE MIDINameDocument PUBLIC \"-//MIDI Manufacturers Association//DTD MIDINameDocument 1.0//EN\" \"http://www.midi.org/dtds/MIDINameDocument10.dtd\">\n"
	         "<MIDINameDocument>\n"
	         "  <Author>setBfree -- Robin Gareus</Author>\n"
	         "  <MasterDeviceNames>\n"
	         "    <Manufacturer>Pather B Music</Manufacturer>\n"
	         "    <Model>%s</Model>\n",
	         model);
}

static void
midnam_footer (FILE* f)
{
	fprintf (f,
	         "  </MasterDeviceNames>\n"
	         "</MIDINameDocument>");
}

static void
midnam_print_cc_cb (const char* fnname, const unsigned char chn, const unsigned char cc, const unsigned char flags, void* arg)
{
	FILE* fp = (FILE*)arg;
	fprintf (fp, "      <Control Type=\"7bit\" Number=\"%d\" Name=\"%s\"/>\n", cc, fnname);
}

static void
midnam_print_pgm_cb (int num, int pc, const char* name, void* arg)
{
	FILE* fp = (FILE*)arg;

	if (strlen (name) == 0) {
		return;
	}

	int         ent = 0;
	char*       escaped;
	const char* tmp = name;
	while ((tmp = strchr (tmp, '&'))) {
		tmp++;
		ent++;
	}

	if (ent == 0) {
		escaped = strdup (name);
	} else {
		const char *t1, *t2;
		escaped    = (char*)malloc ((strlen (name) + ent * 4 + 1) * sizeof (char));
		escaped[0] = '\0';
		t1         = name;
		while ((t2 = strchr (t1, '&'))) {
			strncat (escaped, t1, t2 - t1);
			strcat (escaped, "&amp;");
			t1 = t2 + 1;
		}
		strncat (escaped, t1, strlen (name) - (t1 - name));
	}

	fprintf (fp, "      <Patch Number=\"%03d\" Name=\"%s\" ProgramChange=\"%d\"/>\n",
	         num - 1, escaped, pc - 1);
	free (escaped);
}

static void
midnam_avail_channel (FILE* f, int c)
{
	int i;
	for (i = 1; i <= 16; i++) {
		fprintf (f, "        <AvailableChannel Channel=\"%d\" Available=\"%s\"/>\n",
		         i, (i == c) ? "true" : "false");
	}
}

static void
midnam_channe_set (FILE* f, const char* name, const char* ctrl, int c)
{
	fprintf (f,
	         "    <ChannelNameSet Name=\"%s\">\n"
	         "      <AvailableForChannels>\n",
	         name);
	midnam_avail_channel (f, c + 1);
	fprintf (f,
	         "      </AvailableForChannels>\n"
	         "      <UsesControlNameList Name=\"%s\"/>\n"
	         "      <PatchBank Name=\"Global Patches Bank 1\">\n"
	         "        <UsesPatchNameList Name=\"Programmes\"/>\n"
	         "      </PatchBank>\n"
	         "    </ChannelNameSet>\n",
	         ctrl);
}

void
save_midname (void* instp, FILE* f, char* model)
{
	b_instance* inst = (b_instance*)instp;
	midnam_header (f, model);
	int u, l, p;
	midi_channels (inst->midicfg, &u, &l, &p);

	fprintf (f,
	         "    <CustomDeviceMode Name=\"Default\">\n"
	         "      <ChannelNameSetAssignments>\n"
	         "        <ChannelNameSetAssign Channel=\"%d\" NameSet=\"Upper Manual\"/>\n"
	         "        <ChannelNameSetAssign Channel=\"%d\" NameSet=\"Lower Manual\"/>\n"
	         "        <ChannelNameSetAssign Channel=\"%d\" NameSet=\"Pedals\"/>\n"
	         "      </ChannelNameSetAssignments>\n"
	         "    </CustomDeviceMode>\n",
	         u + 1, l + 1, p + 1);

	midnam_channe_set (f, "Upper Manual", "Controls Upper", u);
	midnam_channe_set (f, "Lower Manual", "Controls Lower", l);
	midnam_channe_set (f, "Pedals", "Controls Pedals", p);

	fprintf (f, "    <PatchNameList Name=\"Programmes\">\n");
	loopProgammes (inst->progs, 1, &midnam_print_pgm_cb, f);
	fprintf (f, "    </PatchNameList>\n");

	fprintf (f, "    <ControlNameList Name=\"Controls Upper\">\n");
	midi_loopCCAssignment (inst->midicfg, 1, midnam_print_cc_cb, f);
	fprintf (f, "    </ControlNameList>\n");

	fprintf (f, "    <ControlNameList Name=\"Controls Lower\">\n");
	midi_loopCCAssignment (inst->midicfg, 2, midnam_print_cc_cb, f);
	fprintf (f, "    </ControlNameList>\n");

	fprintf (f, "    <ControlNameList Name=\"Controls Pedals\">\n");
	midi_loopCCAssignment (inst->midicfg, 4, midnam_print_cc_cb, f);
	fprintf (f, "    </ControlNameList>\n");

	midnam_footer (f);
}
