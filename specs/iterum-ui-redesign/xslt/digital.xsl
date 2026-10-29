<?xml version="1.0" encoding="UTF-8"?>
<!-- digital.xsl - rebuilds template DigitalPanel as specified in panels/digital.md (identity for everything else). -->
<xsl:stylesheet version="3.0"
                xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
                xmlns:xs="http://www.w3.org/2001/XMLSchema"
                exclude-result-prefixes="xs">
  <xsl:include href="cells.xsl"/>
  <xsl:output method="xml" indent="yes" encoding="UTF-8"/>

  <xsl:mode on-no-match="shallow-copy"/>

  <xsl:template match="template[@name='DigitalPanel']">
    <template name="DigitalPanel" class="CViewContainer" origin="0, 0" size="900, 480" background-color="panel"
              transparent="false">
      <!-- core strip -->
      <xsl:call-template name="knob-cell">
        <xsl:with-param name="x" select="12"/>
        <xsl:with-param name="tag" select="'DigitalDelayTime'"/>
        <xsl:with-param name="label" select="'Time'"/>
        <xsl:with-param name="sub-controller" select="'DigitalTimeSyncController'"/>
        <xsl:with-param name="toggle-tag" select="'DigitalTimeMode'"/>
        <xsl:with-param name="toggle-title" select="'Sync'"/>
      </xsl:call-template>
      <xsl:call-template name="knob-cell">
        <xsl:with-param name="x" select="308"/>
        <xsl:with-param name="tag" select="'DigitalFeedback'"/>
        <xsl:with-param name="label" select="'Feedback'"/>
      </xsl:call-template>
      <xsl:call-template name="knob-cell">
        <xsl:with-param name="x" select="604"/>
        <xsl:with-param name="tag" select="'DigitalMix'"/>
        <xsl:with-param name="label" select="'Mix'"/>
      </xsl:call-template>

      <!-- CHARACTER -->
      <xsl:call-template name="section">
        <xsl:with-param name="x" select="12"/>
        <xsl:with-param name="y" select="96"/>
        <xsl:with-param name="w" select="284"/>
        <xsl:with-param name="h" select="180"/>
        <xsl:with-param name="title" select="'CHARACTER'"/>
        <xsl:with-param name="content">
          <xsl:call-template name="segment-cell">
            <xsl:with-param name="x" select="12"/><xsl:with-param name="y" select="24"/>
            <xsl:with-param name="w" select="260"/>
            <xsl:with-param name="tag" select="'DigitalEra'"/>
            <xsl:with-param name="label" select="'Era'"/>
            <xsl:with-param name="segments" select="'Pristine,80s Digital,Lo-Fi'"/>
          </xsl:call-template>
          <!-- Age row: label carries tag DigitalAgeLabel; direct children of the fieldset -->
          <view class="CTextLabel" origin="12, 76" size="196, 16" control-tag="DigitalAgeLabel" title="Age"
                font="label-font" font-color="text-dim" text-alignment="left" transparent="true"/>
          <view class="CParamDisplay" origin="208, 76" size="64, 16" control-tag="DigitalAge" font="value-font"
                font-color="text" text-alignment="right" back-color="~ TransparentCColor"
                frame-color="~ TransparentCColor" style-no-frame="true"/>
          <view class="CSlider" origin="12, 98" size="260, 20" control-tag="DigitalAge" orientation="horizontal"
                draw-frame="true" draw-back="true" draw-value="true" draw-frame-color="slider-frame"
                draw-back-color="slider-track" draw-value-color="accent" frame-width="1" transparent="true"/>
          <xsl:call-template name="segment-cell">
            <xsl:with-param name="x" select="12"/><xsl:with-param name="y" select="128"/>
            <xsl:with-param name="w" select="260"/>
            <xsl:with-param name="tag" select="'DigitalLimiterCharacter'"/>
            <xsl:with-param name="label" select="'Limiter'"/>
            <xsl:with-param name="segments" select="'Soft,Medium,Hard'"/>
          </xsl:call-template>
        </xsl:with-param>
      </xsl:call-template>

      <!-- WAVEFOLD -->
      <xsl:call-template name="section">
        <xsl:with-param name="x" select="308"/>
        <xsl:with-param name="y" select="96"/>
        <xsl:with-param name="w" select="284"/>
        <xsl:with-param name="h" select="180"/>
        <xsl:with-param name="title" select="'WAVEFOLD'"/>
        <xsl:with-param name="content">
          <xsl:call-template name="slider-cell">
            <xsl:with-param name="x" select="12"/><xsl:with-param name="y" select="24"/>
            <xsl:with-param name="w" select="260"/>
            <xsl:with-param name="tag" select="'DigitalWavefoldAmount'"/>
            <xsl:with-param name="label" select="'Wavefold Amount'"/>
          </xsl:call-template>
          <xsl:call-template name="bipolar-cell">
            <xsl:with-param name="x" select="12"/><xsl:with-param name="y" select="76"/>
            <xsl:with-param name="w" select="260"/>
            <xsl:with-param name="tag" select="'DigitalWavefoldSymmetry'"/>
            <xsl:with-param name="label" select="'Wavefold Symmetry'"/>
          </xsl:call-template>
          <xsl:call-template name="segment-cell">
            <xsl:with-param name="x" select="12"/><xsl:with-param name="y" select="128"/>
            <xsl:with-param name="w" select="260"/>
            <xsl:with-param name="tag" select="'DigitalWavefoldType'"/>
            <xsl:with-param name="label" select="'Wavefold Type'"/>
            <xsl:with-param name="segments" select="'Simple,Serge,Buchla,Lockhart'"/>
          </xsl:call-template>
        </xsl:with-param>
      </xsl:call-template>

      <!-- MODULATION -->
      <xsl:call-template name="section">
        <xsl:with-param name="x" select="604"/>
        <xsl:with-param name="y" select="96"/>
        <xsl:with-param name="w" select="284"/>
        <xsl:with-param name="h" select="180"/>
        <xsl:with-param name="title" select="'MODULATION'"/>
        <xsl:with-param name="content">
          <xsl:call-template name="slider-cell">
            <xsl:with-param name="x" select="12"/><xsl:with-param name="y" select="24"/>
            <xsl:with-param name="w" select="260"/>
            <xsl:with-param name="tag" select="'DigitalModDepth'"/>
            <xsl:with-param name="label" select="'Mod Depth'"/>
          </xsl:call-template>
          <xsl:call-template name="slider-cell">
            <xsl:with-param name="x" select="12"/><xsl:with-param name="y" select="76"/>
            <xsl:with-param name="w" select="260"/>
            <xsl:with-param name="tag" select="'DigitalModRate'"/>
            <xsl:with-param name="label" select="'Mod Rate'"/>
          </xsl:call-template>
          <xsl:call-template name="menu-cell">
            <xsl:with-param name="x" select="12"/><xsl:with-param name="y" select="128"/>
            <xsl:with-param name="w" select="260"/>
            <xsl:with-param name="tag" select="'DigitalModWaveform'"/>
            <xsl:with-param name="label" select="'Mod Waveform'"/>
          </xsl:call-template>
        </xsl:with-param>
      </xsl:call-template>

      <!-- STEREO (row 2, column 3) -->
      <xsl:call-template name="section">
        <xsl:with-param name="x" select="604"/>
        <xsl:with-param name="y" select="288"/>
        <xsl:with-param name="w" select="284"/>
        <xsl:with-param name="h" select="180"/>
        <xsl:with-param name="title" select="'STEREO'"/>
        <xsl:with-param name="content">
          <xsl:call-template name="slider-cell">
            <xsl:with-param name="x" select="12"/><xsl:with-param name="y" select="24"/>
            <xsl:with-param name="w" select="260"/>
            <xsl:with-param name="tag" select="'DigitalWidth'"/>
            <xsl:with-param name="label" select="'Width'"/>
          </xsl:call-template>
        </xsl:with-param>
      </xsl:call-template>
    </template>
  </xsl:template>
</xsl:stylesheet>
