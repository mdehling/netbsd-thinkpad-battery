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


static struct tpbat_softc {
	ACPI_HANDLE		 ec_hkey_hdl;
	struct sysctllog	*sc_log;
} tpbat_sc;


static ACPI_STATUS
acpi_eval_int_int(ACPI_HANDLE handle, const char *path,
		ACPI_INTEGER in, ACPI_INTEGER *outp)
{
	ACPI_OBJECT in_obj;
	ACPI_OBJECT_LIST args;
	ACPI_OBJECT out_obj;
	ACPI_BUFFER buf;
	ACPI_STATUS rv;

	if (handle == NULL)
		handle = ACPI_ROOT_OBJECT;

	in_obj.Type = ACPI_TYPE_INTEGER;
	in_obj.Integer.Value = in;

	args.Count = 1;
	args.Pointer = &in_obj;

	memset(&out_obj, 0, sizeof out_obj);
	buf.Pointer = &out_obj;
	buf.Length = sizeof out_obj;

	rv = AcpiEvaluateObject(handle, path, &args, &buf);
	if (ACPI_FAILURE(rv))
		return rv;

	if (buf.Length == 0)
		return AE_NULL_OBJECT;

	if (out_obj.Type != ACPI_TYPE_INTEGER)
		return AE_TYPE;

	if (outp != NULL)
		*outp = out_obj.Integer.Value;

	return AE_OK;
}


static int
tpbat_get_acpi_charge_start(struct tpbat_softc *sc, int *charge_start)
{
	ACPI_INTEGER val;
	ACPI_STATUS rv;

	rv = acpi_eval_int_int(sc->ec_hkey_hdl, "BCTG", 1, &val);
	if (ACPI_FAILURE(rv)) {
		aprint_error("failed to get %s.%s: %s\n",
			acpi_name(sc->ec_hkey_hdl), "BCTG",
			AcpiFormatException(rv));
		return EIO;
	};

	if (val & (1<<31))
		aprint_error("acpi call %s.%s returned failure\n",
			acpi_name(sc->ec_hkey_hdl), "BCTG");

	if (charge_start)
		*charge_start = val & 0x7f;

	return 0;
}

static int
tpbat_get_acpi_charge_stop(struct tpbat_softc *sc, int *charge_stop)
{
	ACPI_INTEGER val;
	ACPI_STATUS rv;

	rv = acpi_eval_int_int(sc->ec_hkey_hdl, "BCSG", 1, &val);
	if (ACPI_FAILURE(rv)) {
		aprint_error("failed to get %s.%s: %s\n",
			acpi_name(sc->ec_hkey_hdl), "BCSG",
			AcpiFormatException(rv));
		return EIO;
	};

	if (val & (1<<31))
		aprint_error("acpi call %s.%s returned failure\n",
			acpi_name(sc->ec_hkey_hdl), "BCSG");

	if (charge_stop)
		*charge_stop = val & 0x7f;

	return 0;
}

static int
tpbat_get_acpi_force_discharge(struct tpbat_softc *sc, int *force_discharge)
{
	ACPI_INTEGER val;
	ACPI_STATUS rv;

	rv = acpi_eval_int_int(sc->ec_hkey_hdl, "BDSG", 1, &val);
	if (ACPI_FAILURE(rv)) {
		aprint_error("failed to get %s.%s: %s\n",
			acpi_name(sc->ec_hkey_hdl), "BDSG",
			AcpiFormatException(rv));
		return EIO;
	};

	if (val & (1<<31))
		aprint_error("acpi call %s.%s returned failure\n",
			acpi_name(sc->ec_hkey_hdl), "BDSG");

	if (force_discharge)
		*force_discharge = val & 0x03;

	return 0;
}

static int
tpbat_set_acpi_charge_start(struct tpbat_softc *sc, int charge_start)
{
	ACPI_STATUS rv;

	rv = acpi_eval_set_integer(sc->ec_hkey_hdl, "BCCS",
		charge_start & 0x7f);
	if (ACPI_FAILURE(rv)) {
		aprint_error("failed to set %s.%s: %s\n",
			acpi_name(sc->ec_hkey_hdl), "BCCS",
			AcpiFormatException(rv));
		return EIO;
	};

	return 0;
}

static int
tpbat_set_acpi_charge_stop(struct tpbat_softc *sc, int charge_stop)
{
	ACPI_STATUS rv;

	rv = acpi_eval_set_integer(sc->ec_hkey_hdl, "BCSS",
		charge_stop & 0x7f);
	if (ACPI_FAILURE(rv)) {
		aprint_error("failed to set %s.%s: %s\n",
			acpi_name(sc->ec_hkey_hdl), "BCSS",
			AcpiFormatException(rv));
		return EIO;
	};

	return 0;
}

static int
tpbat_set_acpi_force_discharge(struct tpbat_softc *sc, int force_discharge)
{
	ACPI_STATUS rv;

	rv = acpi_eval_set_integer(sc->ec_hkey_hdl, "BDSS",
		force_discharge & 0x03);
	if (ACPI_FAILURE(rv)) {
		aprint_error("failed to set %s.%s: %s\n",
			acpi_name(sc->ec_hkey_hdl), "BDSS",
			AcpiFormatException(rv));
		return EIO;
	};

	return 0;
}


static int
tpbat_sysctl_charge_start(SYSCTLFN_ARGS)
{
	struct sysctlnode node = *rnode;
	struct tpbat_softc *sc = node.sysctl_data;
	int charge_start;
	int rv;

	tpbat_get_acpi_charge_start(sc, &charge_start);

	node.sysctl_data = &charge_start;
	rv = sysctl_lookup(SYSCTLFN_CALL(&node));
	if (rv != 0 || newp == NULL)
		return rv;

	if (charge_start < 0 || charge_start > 100)
		return EINVAL;

	tpbat_set_acpi_charge_start(sc, charge_start);

	return 0;
}

static int
tpbat_sysctl_charge_stop(SYSCTLFN_ARGS)
{
	struct sysctlnode node = *rnode;
	struct tpbat_softc *sc = node.sysctl_data;
	int charge_stop;
	int rv;

	tpbat_get_acpi_charge_stop(sc, &charge_stop);

	node.sysctl_data = &charge_stop;
	rv = sysctl_lookup(SYSCTLFN_CALL(&node));
	if (rv != 0 || newp == NULL)
		return rv;

	if (charge_stop < 0 || charge_stop > 100)
		return EINVAL;

	tpbat_set_acpi_charge_stop(sc, charge_stop);

	return 0;
}

static int
tpbat_sysctl_force_discharge(SYSCTLFN_ARGS)
{
	struct sysctlnode node = *rnode;
	struct tpbat_softc *sc = node.sysctl_data;
	int force_discharge;
	int rv;

	tpbat_get_acpi_force_discharge(sc, &force_discharge);

	node.sysctl_data = &force_discharge;
	rv = sysctl_lookup(SYSCTLFN_CALL(&node));
	if (rv != 0 || newp == NULL)
		return rv;

	if (force_discharge < 0 || force_discharge > 3)
		return EINVAL;

	tpbat_set_acpi_force_discharge(sc, force_discharge);

	return 0;
}

static int
tpbat_sysctl_setup(struct tpbat_softc *sc)
{
	const struct sysctlnode *rnode;
	int rv;

	rv = sysctl_createv(&sc->sc_log, 0, NULL, &rnode,
		CTLFLAG_PERMANENT, CTLTYPE_NODE,
		"tpbat", SYSCTL_DESCR("ThinkPad battery controls"),
		NULL, 0, NULL, 0,
		CTL_CREATE, CTL_EOL);
	if (rv != 0) {
		aprint_error("unable to add sysctl nodes\n");
		return rv;
	};

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
	sysctl_createv(&sc->sc_log, 0, &rnode, NULL,
		CTLFLAG_PERMANENT|CTLFLAG_READWRITE, CTLTYPE_INT,
		"force_discharge", SYSCTL_DESCR("force discharge mode"),
		tpbat_sysctl_force_discharge, 0, (void *)sc, 0,
		CTL_CREATE, CTL_EOL);

	return 0;
}


static const char *ec_hkey_path[] = {
	"\\_SB.PCI0.LPC.EC.HKEY",
	"\\_SB.PCI0.LPCB.EC.HKEY",
	"\\_SB.PCI0.LPCB.EC0.HKEY",
	"\\_SB.PCI0.LPCB.H_EC.HKEY",
};

static int
find_ec_hkey_handle(struct tpbat_softc *sc)
{
	ACPI_STATUS rv;
	size_t i;

	for (i = 0; i < __arraycount(ec_hkey_path); i++) {
		rv = AcpiGetHandle(NULL, ec_hkey_path[i], &sc->ec_hkey_hdl);
		if (ACPI_SUCCESS(rv))
			return 0;
	};

	aprint_error("no acpi path for ec.hkey found\n");
	return ENOENT;
}


static int
tpbat_modcmd(modcmd_t cmd, void *args __unused)
{
	switch(cmd) {
		case MODULE_CMD_INIT:
		{	int rv;
			if ((rv = find_ec_hkey_handle(&tpbat_sc)) != 0)
				return rv;
			if ((rv = tpbat_sysctl_setup(&tpbat_sc)) != 0)
				return rv;
		};	break;
		case MODULE_CMD_FINI:
			if (tpbat_set_acpi_charge_start(&tpbat_sc, 0) != 0 ||
			    tpbat_set_acpi_charge_stop(&tpbat_sc, 0) != 0 ||
			    tpbat_set_acpi_force_discharge(&tpbat_sc, 0) != 0)
				aprint_error("failed to reset defaults\n");
			sysctl_teardown(&tpbat_sc.sc_log);
			break;
		default:
			return ENOTTY;
	}
	return 0;
}
