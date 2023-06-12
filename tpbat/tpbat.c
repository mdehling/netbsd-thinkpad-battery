/*
 * Copyright (C) 2023 Malte Dehling.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
#include <sys/module.h>
#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/systm.h>

#include <dev/acpi/acpireg.h>
#include <dev/acpi/acpivar.h>


MODULE(MODULE_CLASS_MISC, tpbat, NULL);


#define ACPI_SET_CHARGE_START	"\\_SB.PCI0.LPC.EC.HKEY.BCCS"
#define ACPI_SET_CHARGE_STOP	"\\_SB.PCI0.LPC.EC.HKEY.BCSS"


static struct tpbat_softc {
	int			 charge_start;
	int			 charge_stop;
	struct sysctllog	*sc_log;
} tpbat_sc;


static int
tpbat_set_acpi_charge_thresholds(struct tpbat_softc *sc)
{
	ACPI_INTEGER charge_start = (ACPI_INTEGER)sc->charge_start;
	ACPI_INTEGER charge_stop = (ACPI_INTEGER)sc->charge_stop;
	ACPI_STATUS rv;

	rv = acpi_eval_set_integer(NULL, ACPI_SET_CHARGE_START, charge_start);
	if (ACPI_FAILURE(rv)) {
		aprint_error("failed to set %s: %s\n",
			ACPI_SET_CHARGE_START, AcpiFormatException(rv));
		return EIO;
	};

	rv = acpi_eval_set_integer(NULL, ACPI_SET_CHARGE_STOP, charge_stop);
	if (ACPI_FAILURE(rv)) {
		aprint_error("failed to set %s: %s\n",
			ACPI_SET_CHARGE_STOP, AcpiFormatException(rv));
		return EIO;
	};

	return 0;
}


static int
tpbat_sysctl_charge_start(SYSCTLFN_ARGS)
{
	struct sysctlnode node = *rnode;
	struct tpbat_softc *sc = node.sysctl_data;
	int val = sc->charge_start;
	int error;

	node.sysctl_data = &val;
	error = sysctl_lookup(SYSCTLFN_CALL(&node));
	if (error || newp == NULL)
		return error;

	if (val < 0 || val > 100)
		return EINVAL;

	sc->charge_start = val;
	sc->charge_stop = (val > sc->charge_stop ? val : sc->charge_stop);

	tpbat_set_acpi_charge_thresholds(sc);

	return error;
}

static int
tpbat_sysctl_charge_stop(SYSCTLFN_ARGS)
{
	struct sysctlnode node = *rnode;
	struct tpbat_softc *sc = node.sysctl_data;
	int val = sc->charge_stop;
	int error;

	node.sysctl_data = &val;
	error = sysctl_lookup(SYSCTLFN_CALL(&node));
	if (error || newp == NULL)
		return error;

	if (val < sc->charge_start || val > 100)
		return EINVAL;

	sc->charge_start = (val < sc->charge_start ? val : sc->charge_start);
	sc->charge_stop = val;

	tpbat_set_acpi_charge_thresholds(sc);

	return error;
}

static void
tpbat_sysctl_setup(struct tpbat_softc *sc)
{
	const struct sysctlnode *rnode;
	int rv;

	rv = sysctl_createv(&sc->sc_log, 0, NULL, &rnode,
		CTLFLAG_PERMANENT, CTLTYPE_NODE,
		"tpbat", SYSCTL_DESCR("ThinkPad battery controls"),
		NULL, 0, NULL, 0,
		CTL_CREATE, CTL_EOL);
	if (rv != 0)
		goto fail;

	sysctl_createv(&sc->sc_log, 0, &rnode, NULL,
		CTLFLAG_PERMANENT|CTLFLAG_READWRITE, CTLTYPE_INT,
		"charge_start", SYSCTL_DESCR("charge start threshold"),
		tpbat_sysctl_charge_start, 0, (void *)sc, 0,
		CTL_CREATE, CTL_EOL);
	sysctl_createv(&sc->sc_log, 0, &rnode, NULL,
		CTLFLAG_PERMANENT|CTLFLAG_READWRITE, CTLTYPE_INT,
		"charge_stop", SYSCTL_DESCR("charge stop threshold"),
		tpbat_sysctl_charge_stop, 0, (void *)sc, 0,
		CTL_CREATE, CTL_EOL);
	return;
 fail:
	aprint_error("unable to add sysctl nodes\n");
}

static int
tpbat_modcmd(modcmd_t cmd, void *arg __unused)
{
	switch(cmd) {
		case MODULE_CMD_INIT:
			tpbat_sysctl_setup(&tpbat_sc);
			break;
		case MODULE_CMD_FINI:
			tpbat_sc.charge_start = 0;
			tpbat_sc.charge_stop = 0;
			if (tpbat_set_acpi_charge_thresholds(&tpbat_sc) != 0)
				aprint_error("failed to reset defaults\n");
			sysctl_teardown(&tpbat_sc.sc_log);
			break;
		default:
			return ENOTTY;
	}
	return 0;
}