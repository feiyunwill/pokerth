/*
 * Generated by asn1c-0.9.22 (http://lionet.info/asn1c)
 * From ASN.1 module "POKERTH-PROTOCOL"
 * 	found in "../../../docs/pokerth.asn1"
 */

#include <asn_internal.h>

#include "EndOfHandMessage.h"

static asn_TYPE_member_t asn_MBR_endOfHandType_3[] = {
	{ ATF_NOFLAGS, 0, offsetof(struct endOfHandType, choice.endOfHandShowCards),
		(ASN_TAG_CLASS_CONTEXT | (0 << 2)),
		-1,	/* IMPLICIT tag at current level */
		&asn_DEF_EndOfHandShowCards,
		0,	/* Defer constraints checking to the member type */
		0,	/* PER is not compiled, use -gen-PER */
		0,
		"endOfHandShowCards"
		},
	{ ATF_NOFLAGS, 0, offsetof(struct endOfHandType, choice.endOfHandHideCards),
		(ASN_TAG_CLASS_CONTEXT | (1 << 2)),
		-1,	/* IMPLICIT tag at current level */
		&asn_DEF_EndOfHandHideCards,
		0,	/* Defer constraints checking to the member type */
		0,	/* PER is not compiled, use -gen-PER */
		0,
		"endOfHandHideCards"
		},
};
static asn_TYPE_tag2member_t asn_MAP_endOfHandType_tag2el_3[] = {
    { (ASN_TAG_CLASS_CONTEXT | (0 << 2)), 0, 0, 0 }, /* endOfHandShowCards at 423 */
    { (ASN_TAG_CLASS_CONTEXT | (1 << 2)), 1, 0, 0 } /* endOfHandHideCards at 425 */
};
static asn_CHOICE_specifics_t asn_SPC_endOfHandType_specs_3 = {
	sizeof(struct endOfHandType),
	offsetof(struct endOfHandType, _asn_ctx),
	offsetof(struct endOfHandType, present),
	sizeof(((struct endOfHandType *)0)->present),
	asn_MAP_endOfHandType_tag2el_3,
	2,	/* Count of tags in the map */
	0,
	2	/* Extensions start */
};
static /* Use -fall-defs-global to expose */
asn_TYPE_descriptor_t asn_DEF_endOfHandType_3 = {
	"endOfHandType",
	"endOfHandType",
	CHOICE_free,
	CHOICE_print,
	CHOICE_constraint,
	CHOICE_decode_ber,
	CHOICE_encode_der,
	CHOICE_decode_xer,
	CHOICE_encode_xer,
	0, 0,	/* No PER support, use "-gen-PER" to enable */
	CHOICE_outmost_tag,
	0,	/* No effective tags (pointer) */
	0,	/* No effective tags (count) */
	0,	/* No tags (pointer) */
	0,	/* No tags (count) */
	0,	/* No PER visible constraints */
	asn_MBR_endOfHandType_3,
	2,	/* Elements count */
	&asn_SPC_endOfHandType_specs_3	/* Additional specs */
};

static asn_TYPE_member_t asn_MBR_EndOfHandMessage_1[] = {
	{ ATF_NOFLAGS, 0, offsetof(struct EndOfHandMessage, gameId),
		(ASN_TAG_CLASS_UNIVERSAL | (2 << 2)),
		0,
		&asn_DEF_NonZeroId,
		0,	/* Defer constraints checking to the member type */
		0,	/* PER is not compiled, use -gen-PER */
		0,
		"gameId"
		},
	{ ATF_NOFLAGS, 0, offsetof(struct EndOfHandMessage, endOfHandType),
		-1 /* Ambiguous tag (CHOICE?) */,
		0,
		&asn_DEF_endOfHandType_3,
		0,	/* Defer constraints checking to the member type */
		0,	/* PER is not compiled, use -gen-PER */
		0,
		"endOfHandType"
		},
};
static ber_tlv_tag_t asn_DEF_EndOfHandMessage_tags_1[] = {
	(ASN_TAG_CLASS_APPLICATION | (26 << 2)),
	(ASN_TAG_CLASS_UNIVERSAL | (16 << 2))
};
static asn_TYPE_tag2member_t asn_MAP_EndOfHandMessage_tag2el_1[] = {
    { (ASN_TAG_CLASS_UNIVERSAL | (2 << 2)), 0, 0, 0 }, /* gameId at 421 */
    { (ASN_TAG_CLASS_CONTEXT | (0 << 2)), 1, 0, 0 }, /* endOfHandShowCards at 423 */
    { (ASN_TAG_CLASS_CONTEXT | (1 << 2)), 1, 0, 0 } /* endOfHandHideCards at 425 */
};
static asn_SEQUENCE_specifics_t asn_SPC_EndOfHandMessage_specs_1 = {
	sizeof(struct EndOfHandMessage),
	offsetof(struct EndOfHandMessage, _asn_ctx),
	asn_MAP_EndOfHandMessage_tag2el_1,
	3,	/* Count of tags in the map */
	0, 0, 0,	/* Optional elements (not needed) */
	1,	/* Start extensions */
	3	/* Stop extensions */
};
asn_TYPE_descriptor_t asn_DEF_EndOfHandMessage = {
	"EndOfHandMessage",
	"EndOfHandMessage",
	SEQUENCE_free,
	SEQUENCE_print,
	SEQUENCE_constraint,
	SEQUENCE_decode_ber,
	SEQUENCE_encode_der,
	SEQUENCE_decode_xer,
	SEQUENCE_encode_xer,
	0, 0,	/* No PER support, use "-gen-PER" to enable */
	0,	/* Use generic outmost tag fetcher */
	asn_DEF_EndOfHandMessage_tags_1,
	sizeof(asn_DEF_EndOfHandMessage_tags_1)
		/sizeof(asn_DEF_EndOfHandMessage_tags_1[0]) - 1, /* 1 */
	asn_DEF_EndOfHandMessage_tags_1,	/* Same as above */
	sizeof(asn_DEF_EndOfHandMessage_tags_1)
		/sizeof(asn_DEF_EndOfHandMessage_tags_1[0]), /* 2 */
	0,	/* No PER visible constraints */
	asn_MBR_EndOfHandMessage_1,
	2,	/* Elements count */
	&asn_SPC_EndOfHandMessage_specs_1	/* Additional specs */
};

