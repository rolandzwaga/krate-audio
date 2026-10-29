<?xml version="1.0" encoding="UTF-8"?>
<!-- spectral.xsl - rebuilds template SpectralPanel per panels/spectral.md; identity for everything else. -->
<xsl:stylesheet version="3.0"
                xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
                xmlns:xs="http://www.w3.org/2001/XMLSchema"
                exclude-result-prefixes="xs">
  <xsl:include href="cells.xsl"/>
  <xsl:output method="xml" indent="yes" encoding="UTF-8"/>

  <xsl:mode on-no-match="shallow-copy"/>

  <xsl:template match="template[@name='SpectralPanel']">
    <template name="SpectralPanel" class="CViewContainer" origin="0, 0" size="900, 480" background-color="panel"
              transparent="false">
      <!-- 2.1 core strip -->
      <xsl:call-template name="knob-cell">
        <xsl:with-param name="x" select="12"/>
        <xsl:with-param name="tag" select="'SpectralBaseDelay'"/>
        <xsl:with-param name="label" select="'Time'"/>
        <xsl:with-param name="sub-controller" select="'SpectralTimeSyncController'"/>
        <xsl:with-param name="toggle-tag" select="'SpectralTimeMode'"/>
        <xsl:with-param name="toggle-title" select="'Sync'"/>
      </xsl:call-template>
      <xsl:call-template name="knob-cell">
        <xsl:with-param name="x" select="308"/>
        <xsl:with-param name="tag" select="'SpectralFeedback'"/>
        <xsl:with-param name="label" select="'Feedback'"/>
      </xsl:call-template>
      <xsl:call-template name="knob-cell">
        <xsl:with-param name="x" select="604"/>
        <xsl:with-param name="tag" select="'SpectralDryWet'"/>
        <xsl:with-param name="label" select="'Mix'"/>
        <xsl:with-param name="toggle-tag" select="'SpectralFreeze'"/>
        <xsl:with-param name="toggle-title" select="'Freeze'"/>
      </xsl:call-template>

      <!-- 2.2 SPREAD -->
      <xsl:call-template name="section">
        <xsl:with-param name="x" select="12"/>
        <xsl:with-param name="y" select="96"/>
        <xsl:with-param name="w" select="284"/>
        <xsl:with-param name="h" select="372"/>
        <xsl:with-param name="title" select="'SPREAD'"/>
        <xsl:with-param name="content">
          <xsl:call-template name="slider-cell">
            <xsl:with-param name="x" select="12"/><xsl:with-param name="y" select="24"/>
            <xsl:with-param name="w" select="260"/>
            <xsl:with-param name="tag" select="'SpectralSpread'"/>
            <xsl:with-param name="label" select="'Spread'"/>
          </xsl:call-template>
          <xsl:call-template name="segment-cell">
            <xsl:with-param name="x" select="12"/><xsl:with-param name="y" select="76"/>
            <xsl:with-param name="w" select="260"/>
            <xsl:with-param name="tag" select="'SpectralSpreadDirection'"/>
            <xsl:with-param name="label" select="'Spread Direction'"/>
            <xsl:with-param name="segments" select="'Low-&gt;High,High-&gt;Low,Center Out'"/>
          </xsl:call-template>
          <xsl:call-template name="segment-cell">
            <xsl:with-param name="x" select="12"/><xsl:with-param name="y" select="128"/>
            <xsl:with-param name="w" select="260"/>
            <xsl:with-param name="tag" select="'SpectralSpreadCurve'"/>
            <xsl:with-param name="label" select="'Spread Curve'"/>
            <xsl:with-param name="segments" select="'Linear,Logarithmic'"/>
          </xsl:call-template>
        </xsl:with-param>
      </xsl:call-template>

      <!-- 2.3 CHARACTER -->
      <xsl:call-template name="section">
        <xsl:with-param name="x" select="308"/>
        <xsl:with-param name="y" select="96"/>
        <xsl:with-param name="w" select="284"/>
        <xsl:with-param name="h" select="372"/>
        <xsl:with-param name="title" select="'CHARACTER'"/>
        <xsl:with-param name="content">
          <xsl:call-template name="bipolar-cell">
            <xsl:with-param name="x" select="12"/><xsl:with-param name="y" select="24"/>
            <xsl:with-param name="w" select="260"/>
            <xsl:with-param name="tag" select="'SpectralFeedbackTilt'"/>
            <xsl:with-param name="label" select="'Feedback Tilt'"/>
          </xsl:call-template>
          <xsl:call-template name="slider-cell">
            <xsl:with-param name="x" select="12"/><xsl:with-param name="y" select="76"/>
            <xsl:with-param name="w" select="260"/>
            <xsl:with-param name="tag" select="'SpectralDiffusion'"/>
            <xsl:with-param name="label" select="'Diffusion'"/>
          </xsl:call-template>
          <xsl:call-template name="segment-cell">
            <xsl:with-param name="x" select="12"/><xsl:with-param name="y" select="128"/>
            <xsl:with-param name="w" select="260"/>
            <xsl:with-param name="tag" select="'SpectralFFTSize'"/>
            <xsl:with-param name="label" select="'FFT Size'"/>
            <xsl:with-param name="segments" select="'512,1024,2048,4096'"/>
          </xsl:call-template>
        </xsl:with-param>
      </xsl:call-template>

      <!-- 2.4 STEREO (pinned cell) -->
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
            <xsl:with-param name="tag" select="'SpectralStereoWidth'"/>
            <xsl:with-param name="label" select="'Width'"/>
          </xsl:call-template>
        </xsl:with-param>
      </xsl:call-template>
    </template>
  </xsl:template>
</xsl:stylesheet>
