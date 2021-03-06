// SPDX-License-Identifier: GPL-2.0-only
/*
 *  PCM DRM helpers
 */
#include <linux/export.h>
#include <drm/drm_edid.h>
#include <sound/pcm.h>
#include <sound/pcm_drm_eld.h>
#include <linux/hdmi.h>

static const unsigned int eld_rates[] = {
	32000,
	44100,
	48000,
	88200,
	96000,
	176400,
	192000,
};

static unsigned int sad_format(const u8 *sad)
{
	return (sad[0] & 0x78) >> 3;
}

static unsigned int sad_max_channels(const u8 *sad)
{
	switch (sad_format(sad)) {
	case HDMI_AUDIO_CODING_TYPE_AC3:
	case HDMI_AUDIO_CODING_TYPE_DTS:
	case HDMI_AUDIO_CODING_TYPE_EAC3:
		return 2;
	case HDMI_AUDIO_CODING_TYPE_DTS_HD:
	case HDMI_AUDIO_CODING_TYPE_MLP:
		return 8;
	case HDMI_AUDIO_CODING_TYPE_PCM:
		return 1 + (sad[0] & 7);
	default:
		return 2; // ignore, map to basic audio
	}
}

static unsigned int sad_rate_mask(const u8 *sad)
{
	switch (sad_format(sad)) {
	case HDMI_AUDIO_CODING_TYPE_AC3:
	case HDMI_AUDIO_CODING_TYPE_DTS:
		return 0x07; // 32-48kHz
	case HDMI_AUDIO_CODING_TYPE_EAC3:
		return 0x7f; // 32-192kHz
	case HDMI_AUDIO_CODING_TYPE_DTS_HD:
	case HDMI_AUDIO_CODING_TYPE_MLP:
		return 0x60; // 176.4, 192kHz
	case HDMI_AUDIO_CODING_TYPE_PCM:
		return sad[1] & 0x7f;
	default:
		return 0x07; // ignore, map to basic audio
	}
}

static int eld_limit_rates(struct snd_pcm_hw_params *params,
			   struct snd_pcm_hw_rule *rule)
{
	struct snd_interval *r = hw_param_interval(params, rule->var);
	const struct snd_interval *c;
	unsigned int rate_mask = 7, i;
	const u8 *sad, *eld = rule->private;

	sad = drm_eld_sad(eld);
	if (sad) {
		c = hw_param_interval_c(params, SNDRV_PCM_HW_PARAM_CHANNELS);

		for (i = drm_eld_sad_count(eld); i > 0; i--, sad += 3) {
			unsigned max_channels = sad_max_channels(sad);

			/*
			 * Exclude SADs which do not include the
			 * requested number of channels.
			 */
			if (c->min <= max_channels)
				rate_mask |= sad_rate_mask(sad);
		}
	}

	return snd_interval_list(r, ARRAY_SIZE(eld_rates), eld_rates,
				 rate_mask);
}

static int eld_limit_channels(struct snd_pcm_hw_params *params,
			      struct snd_pcm_hw_rule *rule)
{
	struct snd_interval *c = hw_param_interval(params, rule->var);
	const struct snd_interval *r;
	struct snd_interval t = { .min = 1, .max = 2, .integer = 1, };
	unsigned int i;
	const u8 *sad, *eld = rule->private;

	sad = drm_eld_sad(eld);
	if (sad) {
		unsigned int rate_mask = 0;

		/* Convert the rate interval to a mask */
		r = hw_param_interval_c(params, SNDRV_PCM_HW_PARAM_RATE);
		for (i = 0; i < ARRAY_SIZE(eld_rates); i++)
			if (r->min <= eld_rates[i] && r->max >= eld_rates[i])
				rate_mask |= BIT(i);

		for (i = drm_eld_sad_count(eld); i > 0; i--, sad += 3)
			if (rate_mask & sad_rate_mask(sad))
				t.max = max(t.max, sad_max_channels(sad));
	}

	return snd_interval_refine(c, &t);
}

int snd_pcm_hw_constraint_eld(struct snd_pcm_runtime *runtime, void *eld)
{
	int ret;

	ret = snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_RATE,
				  eld_limit_rates, eld,
				  SNDRV_PCM_HW_PARAM_CHANNELS, -1);
	if (ret < 0)
		return ret;

	ret = snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_CHANNELS,
				  eld_limit_channels, eld,
				  SNDRV_PCM_HW_PARAM_RATE, -1);

	return ret;
}
EXPORT_SYMBOL_GPL(snd_pcm_hw_constraint_eld);
