<?xml version="1.0" encoding="UTF-8"?>
<!--
  Profundum Phase 14 T013 (FR-030, FR-032; SC-016; plan section 4.10): the Morph Modulation controls 1003 - 1011.
  Identity transform of plugins/profundum/resources/editor.uidesc, run by run-xslt.sh (Saxon-JS, xslt3):
    - nine control-tag rows after MorphRampTime (MorphModSource 1003 ... MorphModCycleSyncRate 1011);
    - Main morph-band: "MORPH MOD" label, Source COptionMenu, Amount ArcKnob and its CParamDisplay readout (P-8);
    - Advanced tab-modulation: "MORPH MODULATION" header and three columns on the Motion block's grid
      (label at x + 0, control at x + 100 / + 120, row pitch 26);
    - tooltips: 1003 and 1009 carry the FR-032 substrings, the Advanced 1002 ArcKnob gains the C-9 sentence.
  The Main-page CSlider MorphPosition keeps its class here (T018 swaps it). The editor baseline (UpdateBanner,
  CheckForUpdatesButton, VersionLabel) is untouched. Refuses to run twice (MorphModSource already present).
  The tooltip literals are copied byte for byte into editor_layout_test.cpp (kMorphModTooltips, kMorphRampTimeTooltip).
-->
<xsl:stylesheet version="3.0" xmlns:xsl="http://www.w3.org/1999/XSL/Transform">
  <xsl:output method="xml" encoding="UTF-8" indent="no"/>
  <xsl:mode on-no-match="shallow-copy"/>

  <!-- Tooltips (FR-032; one sentence naming the range on every other control). Same literal on both pages. -->
  <xsl:variable name="tt1003" select="'Morph Mod Source: what moves the Bass Morph by itself: Envelope per note, Cycle repeating, Velocity of the last note, or the Mod Wheel. With Mod Wheel and a non-zero Amount the wheel moves the morph and stops pushing Mass; at Amount 0 the wheel keeps pushing Mass.'"/>
  <xsl:variable name="tt1004" select="'Morph Mod Amount: how far and in which direction the source moves the Bass Morph, −100 % to +100 %; 0 leaves the morph where you set it.'"/>
  <xsl:variable name="tt1005" select="'Morph Env Attack: how long the morph envelope takes to rise after each note starts, 0.1 ms to 10 s.'"/>
  <xsl:variable name="tt1006" select="'Morph Env Decay: how long the morph envelope takes to fall to the Sustain level, 0.1 ms to 10 s.'"/>
  <xsl:variable name="tt1007" select="'Morph Env Sustain: the level the morph envelope holds until the next note, 0 to 100 %.'"/>
  <xsl:variable name="tt1008" select="'Morph Cycle Shape: waveform of the morph Cycle: Sine, Triangle, Sawtooth, Square, S&amp;H or Smooth Random.'"/>
  <xsl:variable name="tt1009" select="'Morph Cycle Rate: speed of the morph Cycle when Morph Cycle Sync is off, 0.01 to 20 Hz; a Cycle faster than Morph Ramp Time allows becomes a smoothed triangle.'"/>
  <xsl:variable name="tt1010" select="'Morph Cycle Sync: Off or On; On locks the morph Cycle to the host tempo and Morph Cycle Sync Rate sets the note value.'"/>
  <xsl:variable name="tt1011" select="'Morph Cycle Sync Rate: note value of one synced morph Cycle, 1/64T to 4/1D.'"/>
  <xsl:variable name="tt1002add" select="' It also smooths Morph Mod movement.'"/>

  <xsl:template match="/">
    <xsl:if test="//control-tag[@name = 'MorphModSource']">
      <xsl:message terminate="yes">phase14.xsl: MorphModSource is already tagged; the transform was applied before.</xsl:message>
    </xsl:if>
    <xsl:apply-templates/>
  </xsl:template>

  <!-- The header comment's bound-ID count (80 + 9). -->
  <xsl:template match="/vstgui-ui-description/comment()[1]">
    <xsl:comment select="replace(., '80 visible IDs bound', '89 visible IDs bound')"/>
  </xsl:template>

  <!-- Nine control-tag rows after MorphRampTime (1002). -->
  <xsl:template match="control-tags/control-tag[@name = 'MorphRampTime']">
    <xsl:copy-of select="."/>
    <xsl:text>&#10;    </xsl:text><control-tag name="MorphModSource" tag="1003"/>
    <xsl:text>&#10;    </xsl:text><control-tag name="MorphModAmount" tag="1004"/>
    <xsl:text>&#10;    </xsl:text><control-tag name="MorphModAttack" tag="1005"/>
    <xsl:text>&#10;    </xsl:text><control-tag name="MorphModDecay" tag="1006"/>
    <xsl:text>&#10;    </xsl:text><control-tag name="MorphModSustain" tag="1007"/>
    <xsl:text>&#10;    </xsl:text><control-tag name="MorphModCycleShape" tag="1008"/>
    <xsl:text>&#10;    </xsl:text><control-tag name="MorphModCycleRate" tag="1009"/>
    <xsl:text>&#10;    </xsl:text><control-tag name="MorphModCycleSync" tag="1010"/>
    <xsl:text>&#10;    </xsl:text><control-tag name="MorphModCycleSyncRate" tag="1011"/>
  </xsl:template>

  <!-- Main morph-band (636 x 204; the existing controls end at x 448): Source and Amount only (FR-030). -->
  <xsl:template match="view[@uidesc-label = 'morph-band']/view[@control-tag = 'MorphEditTarget']">
    <xsl:copy-of select="."/>
    <xsl:text>&#10;          </xsl:text><view class="CTextLabel" title="MORPH MOD" origin="456, 104" size="120, 16" font="small-font" font-color="text" transparent="true" text-alignment="left" mouse-enabled="false"/>
    <xsl:text>&#10;          </xsl:text><view class="COptionMenu" control-tag="MorphModSource" origin="456, 122" size="124, 20" font="small-font" font-color="text" frame-color="accent" back-color="track" frame-width="1" transparent="false" style-3D-in="false" style-3D-out="false" tooltip="{$tt1003}"/>
    <xsl:text>&#10;          </xsl:text><view class="ArcKnob" control-tag="MorphModAmount" origin="588, 104" size="40, 40" arc-color="accent" guide-color="track" mod-color="highlight" tooltip="{$tt1004}"/>
    <xsl:text>&#10;          </xsl:text><view class="CParamDisplay" control-tag="MorphModAmount" origin="456, 150" size="124, 16" font="label-font" font-color="text" transparent="true" text-alignment="left" tooltip="{$tt1004}"/>
  </xsl:template>

  <!-- Advanced -> Modulation (960 x 516; the Motion block ends near y 181): the MORPH MODULATION block, all nine. -->
  <xsl:template match="view[@uidesc-label = 'tab-modulation']/view[@control-tag = 'MotionDriftSmoothness']">
    <xsl:copy-of select="."/>
    <xsl:text>&#10;        </xsl:text><view class="CTextLabel" title="MORPH MODULATION" origin="12, 196" size="200, 16" font="section-font" font-color="accent" transparent="true" text-alignment="left" mouse-enabled="false"/>
    <!-- column 1 (x 12): Source, Amount -->
    <xsl:text>&#10;        </xsl:text><view class="CTextLabel" title="Source" origin="12, 221" size="96, 16" font="small-font" font-color="text" transparent="true" text-alignment="left" mouse-enabled="false"/>
    <xsl:text>&#10;        </xsl:text><view class="COptionMenu" control-tag="MorphModSource" origin="112, 219" size="196, 20" font="small-font" font-color="text" frame-color="accent" back-color="track" frame-width="1" transparent="false" style-3D-in="false" style-3D-out="false" tooltip="{$tt1003}"/>
    <xsl:text>&#10;        </xsl:text><view class="CTextLabel" title="Amount" origin="12, 247" size="96, 16" font="small-font" font-color="text" transparent="true" text-alignment="left" mouse-enabled="false"/>
    <xsl:text>&#10;        </xsl:text><view class="BipolarSlider" control-tag="MorphModAmount" origin="112, 243" size="196, 24" fill-color="accent" track-color="track" center-tick-color="text-dim" tooltip="{$tt1004}"/>
    <!-- column 2 (x 332): Env Attack, Env Decay, Env Sustain -->
    <xsl:text>&#10;        </xsl:text><view class="CTextLabel" title="Env Attack" origin="332, 221" size="118, 16" font="small-font" font-color="text" transparent="true" text-alignment="left" mouse-enabled="false"/>
    <xsl:text>&#10;        </xsl:text><view class="ArcKnob" control-tag="MorphModAttack" origin="452, 217" size="24, 24" arc-color="accent" guide-color="track" mod-color="highlight" tooltip="{$tt1005}"/>
    <xsl:text>&#10;        </xsl:text><view class="CTextLabel" title="Env Decay" origin="332, 247" size="118, 16" font="small-font" font-color="text" transparent="true" text-alignment="left" mouse-enabled="false"/>
    <xsl:text>&#10;        </xsl:text><view class="ArcKnob" control-tag="MorphModDecay" origin="452, 243" size="24, 24" arc-color="accent" guide-color="track" mod-color="highlight" tooltip="{$tt1006}"/>
    <xsl:text>&#10;        </xsl:text><view class="CTextLabel" title="Env Sustain" origin="332, 273" size="118, 16" font="small-font" font-color="text" transparent="true" text-alignment="left" mouse-enabled="false"/>
    <xsl:text>&#10;        </xsl:text><view class="ArcKnob" control-tag="MorphModSustain" origin="452, 269" size="24, 24" arc-color="accent" guide-color="track" mod-color="highlight" tooltip="{$tt1007}"/>
    <!-- column 3 (x 652): Cycle Shape, Cycle Rate, Cycle Sync, Cycle Sync Rate -->
    <xsl:text>&#10;        </xsl:text><view class="CTextLabel" title="Cycle Shape" origin="652, 221" size="96, 16" font="small-font" font-color="text" transparent="true" text-alignment="left" mouse-enabled="false"/>
    <xsl:text>&#10;        </xsl:text><view class="COptionMenu" control-tag="MorphModCycleShape" origin="752, 219" size="196, 20" font="small-font" font-color="text" frame-color="accent" back-color="track" frame-width="1" transparent="false" style-3D-in="false" style-3D-out="false" tooltip="{$tt1008}"/>
    <xsl:text>&#10;        </xsl:text><view class="CTextLabel" title="Cycle Rate" origin="652, 247" size="118, 16" font="small-font" font-color="text" transparent="true" text-alignment="left" mouse-enabled="false"/>
    <xsl:text>&#10;        </xsl:text><view class="ArcKnob" control-tag="MorphModCycleRate" origin="772, 243" size="24, 24" arc-color="accent" guide-color="track" mod-color="highlight" tooltip="{$tt1009}"/>
    <xsl:text>&#10;        </xsl:text><view class="CTextLabel" title="Cycle Sync" origin="652, 273" size="96, 16" font="small-font" font-color="text" transparent="true" text-alignment="left" mouse-enabled="false"/>
    <xsl:text>&#10;        </xsl:text><view class="CSegmentButton" control-tag="MorphModCycleSync" origin="752, 269" size="196, 24" style="horizontal" selection-mode="Single" font="small-font" text-color="text" text-color-highlighted="bg" frame-color="track" frame-width="1" round-radius="0" gradient="segment-off" gradient-highlighted="segment-on" tooltip="{$tt1010}"/>
    <xsl:text>&#10;        </xsl:text><view class="CTextLabel" title="Cycle Sync Rate" origin="652, 299" size="96, 16" font="small-font" font-color="text" transparent="true" text-alignment="left" mouse-enabled="false"/>
    <xsl:text>&#10;        </xsl:text><view class="COptionMenu" control-tag="MorphModCycleSyncRate" origin="752, 297" size="196, 20" font="small-font" font-color="text" frame-color="accent" back-color="track" frame-width="1" transparent="false" style-3D-in="false" style-3D-out="false" tooltip="{$tt1011}"/>
  </xsl:template>

  <!-- FR-032 (C-9): the Advanced Morph Ramp Time knob names Morph Mod. -->
  <xsl:template match="view[@class = 'ArcKnob'][@control-tag = 'MorphRampTime']/@tooltip">
    <xsl:attribute name="tooltip" select="concat(., $tt1002add)"/>
  </xsl:template>
</xsl:stylesheet>
