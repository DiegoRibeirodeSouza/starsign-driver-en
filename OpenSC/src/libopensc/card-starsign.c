/*
 * Support for G&D StarSign CUT S cards (A.E.T. Europe SafeSign)
 *
 * This driver performs the proprietary DRM handshake and logical
 * channel selection required by the StarSign CUT S token.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <stdlib.h>

#include "internal.h"
#include "asn1.h"
#include "log.h"

static const struct sc_atr_table starsign_atrs[] = {
	{ "3B:F9:96:00:00:81:31:FE:45:53:43:45:37:20:0E:00:20:20:28", NULL, NULL, SC_CARD_TYPE_STARSIGN, 0, NULL },
	{ NULL, NULL, NULL, 0, 0, NULL }
};

static struct sc_card_operations starsign_ops;
static struct sc_card_driver starsign_drv = {
	"G&D StarSign CUT S",
	"starsign",
	&starsign_ops,
	NULL, 0, NULL
};

static const u8 starsign_drm_string[] = "I am A.E.T. Europe B.V. SafeSign or BlueX approved software.";

static int starsign_match_card(sc_card_t *card)
{
	int i;
	i = _sc_match_atr(card, starsign_atrs, &card->type);
	if (i < 0)
		return 0;
	return 1;
}

static int starsign_init(sc_card_t *card)
{
	sc_apdu_t apdu;
	u8 rbuf[SC_MAX_APDU_BUFFER_SIZE];
	int r;

	card->name = "G&D StarSign CUT S";
	card->type = SC_CARD_TYPE_STARSIGN;

	/* 1. First DRM Handshake (ignore result, usually 6D 00) */
	sc_format_apdu(card, &apdu, SC_APDU_CASE_3_SHORT, 0xDA, 0x01, 0x00);
	apdu.data = starsign_drm_string;
	apdu.datalen = sizeof(starsign_drm_string) - 1;
	apdu.lc = apdu.datalen;
	r = sc_transmit_apdu(card, &apdu);
	if (r != SC_SUCCESS) return r;

	/* 2. Select PKCS#15 AID (ignore result) */
	u8 aid[] = { 0xA0, 0x00, 0x00, 0x00, 0x63, 0x50, 0x4B, 0x43, 0x53, 0x2D, 0x31, 0x35 };
	sc_format_apdu(card, &apdu, SC_APDU_CASE_4_SHORT, 0xA4, 0x04, 0x00);
	apdu.data = aid;
	apdu.datalen = sizeof(aid);
	apdu.lc = sizeof(aid);
	apdu.le = 256;
	apdu.resplen = sizeof(rbuf);
	apdu.resp = rbuf;
	r = sc_transmit_apdu(card, &apdu);
	if (r != SC_SUCCESS) return r;

	/* 3. Second DRM Handshake (ignore result) */
	sc_format_apdu(card, &apdu, SC_APDU_CASE_3_SHORT, 0xDA, 0x01, 0x00);
	apdu.data = starsign_drm_string;
	apdu.datalen = sizeof(starsign_drm_string) - 1;
	apdu.lc = apdu.datalen;
	r = sc_transmit_apdu(card, &apdu);
	if (r != SC_SUCCESS) return r;

	/* 4. Open Logical Channel 1 */
	sc_format_apdu(card, &apdu, SC_APDU_CASE_2_SHORT, 0x70, 0x00, 0x00);
	apdu.resp = rbuf;
	apdu.resplen = sizeof(rbuf);
	apdu.le = 1;

	r = sc_transmit_apdu(card, &apdu);
	if (r != SC_SUCCESS) return r;
	
	if (apdu.sw1 == 0x6A && apdu.sw2 == 0x81) {
		/* Channel already open, that's fine */
		r = SC_SUCCESS;
	} else {
		r = sc_check_sw(card, apdu.sw1, apdu.sw2);
		if (r != SC_SUCCESS) return r;
	}

	/* 5. Force CLA=0x01 for all subsequent operations */
	card->cla = 0x01;

	/* 6. Select PKCS#15 AID again on Channel 1 */
	sc_format_apdu(card, &apdu, SC_APDU_CASE_3_SHORT, 0xA4, 0x04, 0x00);
	apdu.data = aid;
	apdu.datalen = sizeof(aid);
	apdu.lc = sizeof(aid);
	r = sc_transmit_apdu(card, &apdu);
	if (r != SC_SUCCESS) return r;
	r = sc_check_sw(card, apdu.sw1, apdu.sw2);
	if (r != SC_SUCCESS) {
		if (apdu.sw1 == 0x6A && apdu.sw2 == 0x86) {
			sc_log(card->ctx, "Card refused AID selection on channel 1 (SW %02X %02X), ignoring as it might be already selected", apdu.sw1, apdu.sw2);
		} else {
			return r;
		}
	}

	/* Force RAW RSA (software padding) or allow on-card padding via hashes.
	   G&D StarSign CUT cards expect either a 256-byte payload (for 2048-bit keys) or a 32-byte hash. */
	card->caps |= SC_CARD_CAP_APDU_EXT;
	card->max_send_size = 2048;
	card->max_recv_size = 256;
	
	/* Patch reader limits directly to prevent apdu.c from falling back to Short APDUs */
	if (card->reader) {
		card->reader->max_send_size = 2048;
		/* Keep recv_size at 256 to prevent card from crashing on large reads during PKCS15 init */
		card->reader->max_recv_size = 256;
	}
	unsigned long alg_flags = SC_ALGORITHM_RSA_RAW | SC_ALGORITHM_RSA_PAD_PKCS1 | SC_ALGORITHM_RSA_HASH_NONE | SC_ALGORITHM_RSA_HASH_MD5 | SC_ALGORITHM_RSA_HASH_SHA1 | SC_ALGORITHM_RSA_HASH_SHA256;
				  
	_sc_card_add_rsa_alg(card, 1024, alg_flags, 0);
	_sc_card_add_rsa_alg(card, 2048, alg_flags, 0);
	_sc_card_add_rsa_alg(card, 4096, alg_flags, 0);

	return SC_SUCCESS;
}

static int starsign_select_file(sc_card_t *card, const sc_path_t *in_path, sc_file_t **file_out)
{
	int r = SC_SUCCESS;
	size_t i;
	struct sc_apdu apdu;

	if (in_path->type != SC_PATH_TYPE_PATH && in_path->type != SC_PATH_TYPE_FROM_CURRENT && in_path->type != SC_PATH_TYPE_FILE_ID) {
		return sc_get_iso7816_driver()->ops->select_file(card, in_path, file_out);
	}

	if (in_path->len % 2 != 0 || in_path->len == 0)
		return SC_ERROR_INVALID_ARGUMENTS;

	/* Loop over every 2 bytes of the path */
	for (i = 0; i < in_path->len; i += 2) {
		/* If there are more than 2 bytes left in the path, the next FID belongs to a DF */
		int p1 = (in_path->len - i > 2) ? 0x01 : 0x00;
		
		sc_format_apdu(card, &apdu, SC_APDU_CASE_3_SHORT, 0xA4, p1, 0x0C);
		apdu.data = &in_path->value[i];
		apdu.datalen = 2;
		apdu.lc = 2;

		r = sc_transmit_apdu(card, &apdu);
		if (r != SC_SUCCESS) return r;
		r = sc_check_sw(card, apdu.sw1, apdu.sw2);

		if (r != SC_SUCCESS) return r;
	}

	if (file_out) {
		sc_file_t *file = sc_file_new();
		if (!file)
			return SC_ERROR_OUT_OF_MEMORY;
		file->id = (in_path->value[in_path->len - 2] << 8) | in_path->value[in_path->len - 1];
		
		/* Set type based on whether we selected a DF or EF in the last step */
		file->type = (in_path->len > 2) ? SC_FILE_TYPE_DF : SC_FILE_TYPE_WORKING_EF;
		file->ef_structure = SC_FILE_EF_TRANSPARENT;
		file->size = 0;
		file->magic = SC_FILE_MAGIC;
		file->path = *in_path;
		*file_out = file;
	}
	return SC_SUCCESS;
}

static int starsign_set_security_env(sc_card_t *card, const sc_security_env_t *env, int res)
{
	sc_apdu_t apdu;
	int r;

	sc_log(card->ctx, "STARSIGN: set_security_env called! Operation: %d", env->operation);

	if (card == NULL || env == NULL) {
		return SC_ERROR_INVALID_ARGUMENTS;
	}

	/* Force max_send_size to prevent apdu.c from falling back to Short APDU chaining 
	   if the PC/SC reader negotiation mistakenly lowered it. */
	card->max_send_size = 2048;
	card->max_recv_size = 2048;
	card->caps |= SC_CARD_CAP_APDU_EXT;

	u8 p2 = 0;
	switch (env->operation) {
	case SC_SEC_OPERATION_AUTHENTICATE:
		p2 = 0xA4;
		break;
	case SC_SEC_OPERATION_DECIPHER:
	case SC_SEC_OPERATION_DERIVE:
		p2 = 0xB8;
		break;
	case SC_SEC_OPERATION_SIGN:
		p2 = 0xB6;
		break;
	default:
		return SC_ERROR_INVALID_ARGUMENTS;
	}

	/* StarSign CUT S hardware specifically requires the exact MSE: SET payload:
	   84 01 01 80 01 02 (Key Reference = 01, Algorithm Reference = 02)
	   Standard OpenSC puts 80 before 84, which the token rejects. */
	
	u8 sbuf[6] = {0x84, 0x01, 0x01, 0x80, 0x01, 0x02};
	
	sc_format_apdu_ex(&apdu, card->cla, 0x22, 0x41, p2, sbuf, sizeof(sbuf), NULL, 0);
	
	sc_log(card->ctx, "STARSIGN: MSE SET constructed payload=84 01 %02X 80 01 %02X", sbuf[2], sbuf[5]);

	r = sc_transmit_apdu(card, &apdu);
	if (r != SC_SUCCESS) return r;
	return sc_check_sw(card, apdu.sw1, apdu.sw2);
}

struct sc_card_driver * sc_get_starsign_driver(void)
{
	struct sc_card_driver *iso_drv = sc_get_iso7816_driver();
	starsign_ops = *iso_drv->ops;
	starsign_ops.init             = starsign_init;
	starsign_ops.match_card       = starsign_match_card;
	starsign_ops.select_file      = starsign_select_file;
	starsign_ops.set_security_env = starsign_set_security_env;
	return &starsign_drv;
}
