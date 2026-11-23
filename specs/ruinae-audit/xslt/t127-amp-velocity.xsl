<?xml version="1.0" encoding="UTF-8"?>
<!--
  t127-amp-velocity.xsl - Velocity > Amp knob (fix plan T1.27, R-018). Identity transform plus:

    1. Control tag: AmpEnvVelocityAmount (708) after AmpEnvRelease (703).
    2. SOUND tab, ENVELOPES fieldset, AMP envelope group (EnvGroupAmp, 440 x 181): a fifth knob right of the
       ADSR row, after the Release knob (origin 373, 143) and its "R" label (origin 369, 171):
         AmpEnvVelocityAmount knob  origin 410, 143  size 28 x 28   default 1.0 = level follows velocity
         "VEL" label                origin 406, 171  size 28 x 10

  Apply:  npx xslt3 -xsl:specs/ruinae-audit/xslt/t127-amp-velocity.xsl
                    -s:plugins/ruinae/resources/editor.uidesc -o:OUT
  No strip-space and no indent: whitespace text nodes are copied, so only the inserted elements are new. Then restore
  the file's own formatting on OUT (CRLF line ends, the multi-line attribute layout of the elements the serializer
  rewrote on one line): the inserted lines are the only change to keep.
-->
<xsl:stylesheet version="3.0"
                xmlns:xsl="http://www.w3.org/1999/XSL/Transform">

  <xsl:output method="xml" encoding="UTF-8" indent="no"/>
  <xsl:mode on-no-match="shallow-copy"/>

  <!-- 1. control tag -->
  <xsl:template match="control-tags/control-tag[@name = 'AmpEnvRelease']">
    <xsl:if test="@tag ne '703'">
      <xsl:message terminate="yes">AmpEnvRelease has tag <xsl:value-of select="@tag"/>, expected 703</xsl:message>
    </xsl:if>
    <xsl:copy-of select="."/>
    <xsl:text>&#10;        </xsl:text>
    <control-tag name="AmpEnvVelocityAmount" tag="708"/>
  </xsl:template>

  <!-- 2. AMP envelope group: Velocity > Amp knob after the "R" label -->
  <xsl:template match="view[@custom-view-name = 'EnvGroupAmp']/view[@class = 'CTextLabel' and @title = 'R']">
    <xsl:if test="@origin ne '369, 171' or @size ne '28, 10'">
      <xsl:message terminate="yes">The Amp "R" label is at <xsl:value-of select="@origin"/> / <xsl:value-of select="@size"/>, expected 369, 171 / 28, 10</xsl:message>
    </xsl:if>
    <xsl:copy-of select="."/>
    <xsl:text>&#10;                </xsl:text>
    <xsl:comment> Velocity > Amp: how far note velocity sets the level </xsl:comment>
    <xsl:text>&#10;                </xsl:text>
    <view class="ArcKnob" origin="410, 143" size="28, 28" control-tag="AmpEnvVelocityAmount" default-value="1.0" tooltip="Velocity &gt; Amp: how far note velocity sets the level" arc-color="amp-env" guide-color="knob-guide"/>
    <xsl:text>&#10;                </xsl:text>
    <view class="CTextLabel" origin="406, 171" size="28, 10" font="~ NormalFontSmaller" font-color="amp-env" text-alignment="center" transparent="true" title="VEL"/>
  </xsl:template>

</xsl:stylesheet>
