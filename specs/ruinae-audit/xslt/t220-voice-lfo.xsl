<?xml version="1.0" encoding="UTF-8"?>
<!--
  t220-voice-lfo.xsl - Voice LFO Rate and Shape controls (fix plan T2.20, R-093). Identity transform plus:

    1. Control tags: VoiceLfoRate (2900) and VoiceLfoShape (2901) after TransientDecay.
    2. MOD tab, MODULATION SOURCES fieldset (origin 8, 0, 540 x 448): the header row right of the ModSourceDropdown
       (origin 8, 18, 160 x 22), above the source view switch (starts at y 46):
         "Voice LFO" label  origin 232, 22  size 64 x 14
         VoiceLfoRate knob  origin 300, 15  size 28 x 28   default 0.5407 = 1 Hz on the 0.01-50 Hz log map
         VoiceLfoShape menu origin 336, 18  size 112 x 22  default 0 = Sine
       The Voice LFO is a per-voice source (voice routes), so it is not one of the global sources in the dropdown.

  Apply:  npx xslt3 -xsl:specs/ruinae-audit/xslt/t220-voice-lfo.xsl
                    -s:plugins/ruinae/resources/editor.uidesc -o:OUT
  No strip-space and no indent: whitespace text nodes are copied, so only the inserted elements are new. Then restore
  the file's own formatting on OUT (CRLF line ends, the multi-line attribute layout of the elements the serializer
  rewrote on one line): the inserted lines are the only change to keep.
-->
<xsl:stylesheet version="3.0"
                xmlns:xsl="http://www.w3.org/1999/XSL/Transform">

  <xsl:output method="xml" encoding="UTF-8" indent="no"/>
  <xsl:mode on-no-match="shallow-copy"/>

  <!-- 1. control tags -->
  <xsl:template match="control-tags/control-tag[@name = 'TransientDecay']">
    <xsl:if test="@tag ne '2702'">
      <xsl:message terminate="yes">TransientDecay has tag <xsl:value-of select="@tag"/>, expected 2702</xsl:message>
    </xsl:if>
    <xsl:copy-of select="."/>
    <xsl:text>&#10;&#10;        </xsl:text>
    <xsl:comment> Voice LFO (per-voice mod source) </xsl:comment>
    <xsl:text>&#10;        </xsl:text>
    <control-tag name="VoiceLfoRate" tag="2900"/>
    <xsl:text>&#10;        </xsl:text>
    <control-tag name="VoiceLfoShape" tag="2901"/>
  </xsl:template>

  <!-- 2. MOD tab header row: Voice LFO rate + shape right of the source dropdown -->
  <xsl:template match="template[@name = 'Tab_Mod']/view[@class = 'FieldsetContainer' and @fieldset-title = 'MODULATION SOURCES']/view[@custom-view-name = 'ModSourceDropdown']">
    <xsl:if test="@origin ne '8, 18' or @size ne '160, 22'">
      <xsl:message terminate="yes">ModSourceDropdown is at <xsl:value-of select="@origin"/> / <xsl:value-of select="@size"/>, expected 8, 18 / 160, 22</xsl:message>
    </xsl:if>
    <xsl:copy-of select="."/>
    <xsl:text>&#10;&#10;            </xsl:text>
    <xsl:comment> Voice LFO: rate and shape of the per-voice "Voice LFO" route source </xsl:comment>
    <xsl:text>&#10;            </xsl:text>
    <view class="CTextLabel" origin="232, 22" size="64, 14" title="Voice LFO" font="~ NormalFontSmaller" font-color="text-secondary" text-alignment="right" transparent="true"/>
    <xsl:text>&#10;            </xsl:text>
    <view class="ArcKnob" origin="300, 15" size="28, 28" control-tag="VoiceLfoRate" default-value="0.5407" tooltip="Voice LFO rate" arc-color="modulation" guide-color="knob-guide"/>
    <xsl:text>&#10;            </xsl:text>
    <view class="COptionMenu" origin="336, 18" size="112, 22" control-tag="VoiceLfoShape" default-value="0" tooltip="Voice LFO shape" font="~ NormalFontSmaller" font-color="text-menu" back-color="bg-dropdown" frame-color="frame-dropdown"/>
  </xsl:template>

</xsl:stylesheet>
